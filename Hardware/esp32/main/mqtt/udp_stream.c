#include "udp_stream.h"

#include "esp_log.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/timing.h"

#if defined(__has_include)
#if __has_include("../app_secrets.h")
#include "../app_secrets.h"
#else
#include "../app_secrets.defaults.h"
#endif
#else
#include "../app_secrets.defaults.h"
#endif

#if !defined(CONFIG_MBEDTLS_PSK_MODES) || !defined(CONFIG_MBEDTLS_SSL_PROTO_DTLS)
#error "Enable CONFIG_MBEDTLS_PSK_MODES and CONFIG_MBEDTLS_SSL_PROTO_DTLS for DTLS thermal streaming."
#endif

#ifndef APP_DTLS_TARGET_HOST
#define APP_DTLS_TARGET_HOST ""
#endif

#ifndef APP_DTLS_TARGET_PORT
#define APP_DTLS_TARGET_PORT 5005
#endif

#ifndef APP_DTLS_PSK_IDENTITY
#define APP_DTLS_PSK_IDENTITY ""
#endif

#ifndef APP_DTLS_PSK_KEY_HEX
#define APP_DTLS_PSK_KEY_HEX ""
#endif

#define DTLS_STRINGIFY_IMPL(x) #x
#define DTLS_STRINGIFY(x) DTLS_STRINGIFY_IMPL(x)

static const char *TAG = "udp_stream";
static const TickType_t UDP_SEND_RETRY_DELAY_TICKS = 1U;
static const int UDP_SEND_RETRY_COUNT = 3;
static const uint32_t DTLS_HANDSHAKE_MIN_MS = 1000U;
static const uint32_t DTLS_HANDSHAKE_MAX_MS = 8000U;
static const uint32_t DTLS_READ_TIMEOUT_MS = 1000U;
static const size_t DTLS_MAX_PSK_BYTES = 64U;
static const size_t DTLS_ERRBUF_LEN = 128U;

static mbedtls_net_context s_dtls_net;
static mbedtls_ssl_context s_dtls_ssl;
static mbedtls_ssl_config s_dtls_conf;
static mbedtls_ctr_drbg_context s_dtls_ctr_drbg;
static mbedtls_entropy_context s_dtls_entropy;
static mbedtls_timing_delay_context s_dtls_timer;

static bool s_udp_enabled = false;
static bool s_dtls_context_initialized = false;
static bool s_dtls_configured = false;

static void dtlsLogError(const char *message, int err)
{
    char errbuf[DTLS_ERRBUF_LEN] = {0};
    mbedtls_strerror(err, errbuf, sizeof(errbuf));
    ESP_LOGW(TAG, "%s ret=%d (%s)", message, err, errbuf);
}

static int dtlsHexNibble(char ch)
{
    if ((ch >= '0') && (ch <= '9')) {
        return ch - '0';
    }
    if ((ch >= 'a') && (ch <= 'f')) {
        return 10 + (ch - 'a');
    }
    if ((ch >= 'A') && (ch <= 'F')) {
        return 10 + (ch - 'A');
    }
    return -1;
}

static int dtlsParseHex(const char *hex, unsigned char *out, size_t out_size)
{
    size_t hex_len = 0U;
    size_t out_len = 0U;

    if ((hex == NULL) || (out == NULL)) {
        return -1;
    }

    hex_len = strlen(hex);
    if ((hex_len == 0U) || ((hex_len % 2U) != 0U)) {
        return -1;
    }

    out_len = hex_len / 2U;
    if (out_len > out_size) {
        return -1;
    }

    for (size_t i = 0U; i < out_len; i++) {
        const int hi = dtlsHexNibble(hex[i * 2U]);
        const int lo = dtlsHexNibble(hex[(i * 2U) + 1U]);
        if ((hi < 0) || (lo < 0)) {
            return -1;
        }
        out[i] = (unsigned char)((hi << 4) | lo);
    }

    return (int)out_len;
}

static void dtlsContextInitOnce(void)
{
    if (s_dtls_context_initialized) {
        return;
    }

    mbedtls_net_init(&s_dtls_net);
    mbedtls_ssl_init(&s_dtls_ssl);
    mbedtls_ssl_config_init(&s_dtls_conf);
    mbedtls_ctr_drbg_init(&s_dtls_ctr_drbg);
    mbedtls_entropy_init(&s_dtls_entropy);
    memset(&s_dtls_timer, 0, sizeof(s_dtls_timer));
    s_dtls_context_initialized = true;
}

void udpStreamReset(void)
{
    if (s_dtls_configured) {
        (void)mbedtls_ssl_close_notify(&s_dtls_ssl);
    }

    s_udp_enabled = false;
    s_dtls_configured = false;

    if (s_dtls_context_initialized) {
        mbedtls_ssl_session_reset(&s_dtls_ssl);
        mbedtls_ssl_free(&s_dtls_ssl);
        mbedtls_ssl_config_free(&s_dtls_conf);
        mbedtls_ctr_drbg_free(&s_dtls_ctr_drbg);
        mbedtls_entropy_free(&s_dtls_entropy);
        mbedtls_net_free(&s_dtls_net);
        memset(&s_dtls_timer, 0, sizeof(s_dtls_timer));
        s_dtls_context_initialized = false;
    }
}

static bool udpStreamConfigValid(void)
{
    return (strlen(APP_DTLS_TARGET_HOST) > 0U) &&
           (APP_DTLS_TARGET_PORT > 0) &&
           (strlen(APP_DTLS_PSK_IDENTITY) > 0U) &&
           (strlen(APP_DTLS_PSK_KEY_HEX) > 0U);
}

bool udpStreamIsEnabled(void)
{
    return udpStreamConfigValid();
}

bool udpStreamIsReady(void)
{
    return s_udp_enabled && s_dtls_configured;
}

static int dtlsConfigureClient(void)
{
    static const char *pers = "thermal_dtls_client";
    unsigned char psk[DTLS_MAX_PSK_BYTES] = {0};
    const int psk_len = dtlsParseHex(APP_DTLS_PSK_KEY_HEX, psk, sizeof(psk));
    int ret = 0;

    if (psk_len <= 0) {
        ESP_LOGE(TAG, "invalid APP_DTLS_PSK_KEY_HEX");
        return -1;
    }

    dtlsContextInitOnce();

    ret = mbedtls_ctr_drbg_seed(&s_dtls_ctr_drbg,
                                mbedtls_entropy_func,
                                &s_dtls_entropy,
                                (const unsigned char *)pers,
                                strlen(pers));
    if (ret != 0) {
        dtlsLogError("mbedtls_ctr_drbg_seed() failed", ret);
        return ret;
    }

    ret = mbedtls_net_connect(&s_dtls_net,
                              APP_DTLS_TARGET_HOST,
                              DTLS_STRINGIFY(APP_DTLS_TARGET_PORT),
                              MBEDTLS_NET_PROTO_UDP);
    if (ret != 0) {
        dtlsLogError("mbedtls_net_connect() failed", ret);
        return ret;
    }

    ret = mbedtls_ssl_config_defaults(&s_dtls_conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        dtlsLogError("mbedtls_ssl_config_defaults() failed", ret);
        return ret;
    }

    mbedtls_ssl_conf_authmode(&s_dtls_conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&s_dtls_conf, mbedtls_ctr_drbg_random, &s_dtls_ctr_drbg);
    mbedtls_ssl_conf_handshake_timeout(&s_dtls_conf,
                                       DTLS_HANDSHAKE_MIN_MS,
                                       DTLS_HANDSHAKE_MAX_MS);
    mbedtls_ssl_conf_read_timeout(&s_dtls_conf, DTLS_READ_TIMEOUT_MS);

    ret = mbedtls_ssl_conf_psk(&s_dtls_conf,
                               psk,
                               (size_t)psk_len,
                               (const unsigned char *)APP_DTLS_PSK_IDENTITY,
                               strlen(APP_DTLS_PSK_IDENTITY));
    if (ret != 0) {
        dtlsLogError("mbedtls_ssl_conf_psk() failed", ret);
        return ret;
    }

    ret = mbedtls_ssl_setup(&s_dtls_ssl, &s_dtls_conf);
    if (ret != 0) {
        dtlsLogError("mbedtls_ssl_setup() failed", ret);
        return ret;
    }

    mbedtls_ssl_set_bio(&s_dtls_ssl,
                        &s_dtls_net,
                        mbedtls_net_send,
                        mbedtls_net_recv,
                        mbedtls_net_recv_timeout);
    mbedtls_ssl_set_timer_cb(&s_dtls_ssl,
                             &s_dtls_timer,
                             mbedtls_timing_set_delay,
                             mbedtls_timing_get_delay);

    return 0;
}

static int dtlsPerformHandshake(void)
{
    int ret = 0;
    int hello_verify_retries = 0;

    for (;;) {
        ret = mbedtls_ssl_handshake(&s_dtls_ssl);
        if (ret == 0) {
            return 0;
        }

        if ((ret == MBEDTLS_ERR_SSL_WANT_READ) ||
            (ret == MBEDTLS_ERR_SSL_WANT_WRITE)) {
            continue;
        }

#ifdef MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED
        if (ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
            if (hello_verify_retries >= 2) {
                dtlsLogError("DTLS hello verify retry limit reached", ret);
                return ret;
            }

            hello_verify_retries++;
            ret = mbedtls_ssl_session_reset(&s_dtls_ssl);
            if (ret != 0) {
                dtlsLogError("mbedtls_ssl_session_reset() failed", ret);
                return ret;
            }

            mbedtls_ssl_set_bio(&s_dtls_ssl,
                                &s_dtls_net,
                                mbedtls_net_send,
                                mbedtls_net_recv,
                                mbedtls_net_recv_timeout);
            mbedtls_ssl_set_timer_cb(&s_dtls_ssl,
                                     &s_dtls_timer,
                                     mbedtls_timing_set_delay,
                                     mbedtls_timing_get_delay);
            continue;
        }
#endif

        return ret;
    }
}

bool udpStreamInit(void)
{
    if (udpStreamIsReady()) {
        return true;
    }

    if (!udpStreamConfigValid()) {
        ESP_LOGI(TAG, "DTLS stream disabled (target host or PSK not configured)");
        return false;
    }

    udpStreamReset();

    if (dtlsConfigureClient() != 0) {
        udpStreamReset();
        return false;
    }

    {
        const int handshake_ret = dtlsPerformHandshake();
        if (handshake_ret != 0) {
            dtlsLogError("DTLS handshake failed", handshake_ret);
            udpStreamReset();
            return false;
        }
    }

    s_dtls_configured = true;
    s_udp_enabled = true;
    ESP_LOGI(TAG,
             "DTLS stream target=%s:%d identity=%s",
             APP_DTLS_TARGET_HOST,
             (int)APP_DTLS_TARGET_PORT,
             APP_DTLS_PSK_IDENTITY);
    return true;
}

int udpStreamSend(const void *payload, size_t len)
{
    int ret = -1;

    if ((payload == NULL) || (len == 0U)) {
        return -1;
    }

    if (!udpStreamIsReady() && !udpStreamInit()) {
        return -1;
    }

    for (int attempt = 0; attempt < UDP_SEND_RETRY_COUNT; attempt++) {
        ret = mbedtls_ssl_write(&s_dtls_ssl, (const unsigned char *)payload, len);
        if (ret > 0) {
            if ((size_t)ret != len) {
                ESP_LOGW(TAG, "mbedtls_ssl_write() partial send ret=%d expected=%u",
                         ret,
                         (unsigned int)len);
                udpStreamReset();
                return -1;
            }
            return ret;
        }

        if ((ret == MBEDTLS_ERR_SSL_WANT_READ) || (ret == MBEDTLS_ERR_SSL_WANT_WRITE)) {
            vTaskDelay(UDP_SEND_RETRY_DELAY_TICKS);
            continue;
        }

        dtlsLogError("mbedtls_ssl_write() failed", ret);

        if ((ret == MBEDTLS_ERR_SSL_TIMEOUT)
#ifdef MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY
            || (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
#endif
           ) {
            udpStreamReset();
            break;
        }

        if (attempt + 1 < UDP_SEND_RETRY_COUNT) {
            vTaskDelay(UDP_SEND_RETRY_DELAY_TICKS);
        }
    }

    return -1;
}

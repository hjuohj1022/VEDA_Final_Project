#include "udp_stream.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
#define DTLS_MAX_PSK_BYTES 64U
#define DTLS_ERRBUF_LEN 128U

static const char *TAG = "udp_stream";
static const TickType_t UDP_SEND_RETRY_DELAY_TICKS = 1U;
static const int UDP_SEND_RETRY_COUNT = 6;
static const TickType_t UDP_SEND_ENOMEM_BASE_DELAY_TICKS = pdMS_TO_TICKS(8U);
static const TickType_t UDP_SEND_POST_HANDSHAKE_DELAY_TICKS = pdMS_TO_TICKS(20U);
static const int64_t DTLS_TX_CONGESTION_HOLD_US = 500000LL;
static const uint32_t DTLS_HANDSHAKE_MIN_MS = 1000U;
static const uint32_t DTLS_HANDSHAKE_MAX_MS = 8000U;
static const uint32_t DTLS_READ_TIMEOUT_MS = 1000U;
static const uint32_t DTLS_RETRY_BACKOFF_MS = 10000U;

static mbedtls_net_context s_dtls_net;
static mbedtls_ssl_context s_dtls_ssl;
static mbedtls_ssl_config s_dtls_conf;
static mbedtls_ctr_drbg_context s_dtls_ctr_drbg;
static mbedtls_entropy_context s_dtls_entropy;
static mbedtls_timing_delay_context s_dtls_timer;
static SemaphoreHandle_t s_dtls_mutex = NULL;

static bool s_udp_enabled = false;
static bool s_dtls_context_initialized = false;
static bool s_dtls_configured = false;
static bool s_dtls_ever_connected = false;
static volatile bool s_dtls_reset_requested = false;
static int64_t s_dtls_send_ready_after_us = 0;
static int64_t s_dtls_tx_congested_until_us = 0;
static int64_t s_dtls_retry_after_us = 0;

static void dtlsLogError(const char *message, int err)
{
    char errbuf[DTLS_ERRBUF_LEN] = {0};
    mbedtls_strerror(err, errbuf, sizeof(errbuf));
    if ((err == MBEDTLS_ERR_NET_SEND_FAILED) ||
        (err == MBEDTLS_ERR_NET_RECV_FAILED) ||
        (err == MBEDTLS_ERR_NET_CONN_RESET)) {
        const int saved_errno = errno;
        ESP_LOGW(TAG,
                 "%s ret=%d (%s) errno=%d (%s)",
                 message,
                 err,
                 errbuf,
                 saved_errno,
                 strerror(saved_errno));
        return;
    }

    ESP_LOGW(TAG, "%s ret=%d (%s)", message, err, errbuf);
}

static void dtlsLogHeap(const char *message)
{
    ESP_LOGI(TAG,
             "%s free_heap=%lu min_heap=%lu largest_8bit=%lu",
             message,
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void dtlsSetRetryDelayMs(uint32_t delay_ms)
{
    if (delay_ms == 0U) {
        s_dtls_retry_after_us = 0;
        return;
    }

    s_dtls_retry_after_us = esp_timer_get_time() + ((int64_t)delay_ms * 1000LL);
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

static bool dtlsEnsureMutex(void)
{
    if (s_dtls_mutex != NULL) {
        return true;
    }

    s_dtls_mutex = xSemaphoreCreateMutex();
    return (s_dtls_mutex != NULL);
}

static void dtlsResetLocked(void)
{
    if (s_dtls_configured) {
        (void)mbedtls_ssl_close_notify(&s_dtls_ssl);
    }

    s_udp_enabled = false;
    s_dtls_configured = false;
    s_dtls_reset_requested = false;
    s_dtls_send_ready_after_us = 0;
    s_dtls_tx_congested_until_us = 0;

    if (s_dtls_context_initialized) {
        (void)mbedtls_ssl_session_reset(&s_dtls_ssl);
        mbedtls_ssl_free(&s_dtls_ssl);
        mbedtls_ssl_config_free(&s_dtls_conf);
        mbedtls_ctr_drbg_free(&s_dtls_ctr_drbg);
        mbedtls_entropy_free(&s_dtls_entropy);
        mbedtls_net_free(&s_dtls_net);
        memset(&s_dtls_timer, 0, sizeof(s_dtls_timer));
        s_dtls_context_initialized = false;
    }
}

static bool dtlsLock(TickType_t timeout_ticks)
{
    return dtlsEnsureMutex() &&
           (xSemaphoreTake(s_dtls_mutex, timeout_ticks) == pdTRUE);
}

static void dtlsUnlock(void)
{
    if (s_dtls_mutex != NULL) {
        (void)xSemaphoreGive(s_dtls_mutex);
    }
}

static void dtlsApplyPendingResetLocked(void)
{
    if (s_dtls_reset_requested) {
        dtlsResetLocked();
    }
}

void udpStreamRequestReset(void)
{
    if (!dtlsLock(0U)) {
        s_dtls_reset_requested = true;
        return;
    }

    s_udp_enabled = false;
    s_dtls_configured = false;
    s_dtls_reset_requested = true;
    dtlsUnlock();
}

void udpStreamReset(void)
{
    if (!dtlsLock(portMAX_DELAY)) {
        s_udp_enabled = false;
        s_dtls_configured = false;
        s_dtls_reset_requested = true;
        return;
    }

    dtlsResetLocked();
    dtlsUnlock();
}

void udpStreamDeferInit(uint32_t delay_ms)
{
    if (!dtlsLock(portMAX_DELAY)) {
        dtlsSetRetryDelayMs(delay_ms);
        return;
    }

    dtlsSetRetryDelayMs(delay_ms);
    dtlsUnlock();
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

bool udpStreamHasConnectedOnce(void)
{
    bool connected_once = false;

    if (!dtlsLock(portMAX_DELAY)) {
        return false;
    }

    connected_once = s_dtls_ever_connected;
    dtlsUnlock();
    return connected_once;
}

bool udpStreamIsCongested(void)
{
    bool congested = false;

    if (!dtlsLock(portMAX_DELAY)) {
        return false;
    }

    congested = (s_dtls_tx_congested_until_us != 0) &&
                (esp_timer_get_time() < s_dtls_tx_congested_until_us);
    dtlsUnlock();
    return congested;
}

bool udpStreamIsReady(void)
{
    bool ready = false;

    if (!dtlsLock(portMAX_DELAY)) {
        return false;
    }

    dtlsApplyPendingResetLocked();
    ready = s_udp_enabled && s_dtls_configured;
    dtlsUnlock();
    return ready;
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
    bool ok = false;

    if (!dtlsLock(portMAX_DELAY)) {
        ESP_LOGE(TAG, "DTLS mutex unavailable");
        return false;
    }

    dtlsApplyPendingResetLocked();

    if (s_udp_enabled && s_dtls_configured) {
        dtlsUnlock();
        return true;
    }

    if (!udpStreamConfigValid()) {
        ESP_LOGI(TAG, "DTLS stream disabled (target host or PSK not configured)");
        dtlsUnlock();
        return false;
    }

    const int64_t now_us = esp_timer_get_time();
    if ((s_dtls_retry_after_us != 0) && (now_us < s_dtls_retry_after_us)) {
        dtlsUnlock();
        return false;
    }

    dtlsResetLocked();
    dtlsLogHeap("DTLS init begin");

    if (dtlsConfigureClient() != 0) {
        dtlsSetRetryDelayMs(DTLS_RETRY_BACKOFF_MS);
        dtlsLogHeap("DTLS init failed");
        dtlsResetLocked();
        dtlsUnlock();
        return false;
    }

    const int handshake_ret = dtlsPerformHandshake();
    if (handshake_ret != 0) {
        dtlsLogError("DTLS handshake failed", handshake_ret);
        dtlsSetRetryDelayMs(DTLS_RETRY_BACKOFF_MS);
        dtlsLogHeap("DTLS handshake failed");
        dtlsResetLocked();
        dtlsUnlock();
        return false;
    }

    dtlsApplyPendingResetLocked();
    if (s_dtls_reset_requested || s_dtls_context_initialized == false) {
        dtlsSetRetryDelayMs(DTLS_RETRY_BACKOFF_MS);
        dtlsUnlock();
        return false;
    }

    s_dtls_retry_after_us = 0;
    s_dtls_configured = true;
    s_dtls_ever_connected = true;
    s_dtls_send_ready_after_us = esp_timer_get_time() +
                                 ((int64_t)UDP_SEND_POST_HANDSHAKE_DELAY_TICKS * 1000LL *
                                  portTICK_PERIOD_MS);
    s_udp_enabled = true;
    dtlsLogHeap("DTLS init OK");
    ESP_LOGI(TAG,
             "DTLS stream target=%s:%d identity=%s",
             APP_DTLS_TARGET_HOST,
             (int)APP_DTLS_TARGET_PORT,
             APP_DTLS_PSK_IDENTITY);
    ok = true;
    dtlsUnlock();
    return ok;
}

int udpStreamSend(const void *payload, size_t len)
{
    int ret = -1;

    if ((payload == NULL) || (len == 0U)) {
        return -1;
    }

    if (!udpStreamInit()) {
        return -1;
    }

    if (!dtlsLock(portMAX_DELAY)) {
        ESP_LOGE(TAG, "DTLS mutex unavailable");
        return -1;
    }

    dtlsApplyPendingResetLocked();
    if (!(s_udp_enabled && s_dtls_configured)) {
        dtlsUnlock();
        return -1;
    }

    if ((s_dtls_send_ready_after_us != 0) &&
        (esp_timer_get_time() < s_dtls_send_ready_after_us)) {
        const int64_t wait_us = s_dtls_send_ready_after_us - esp_timer_get_time();
        const TickType_t wait_ticks = pdMS_TO_TICKS((uint32_t)((wait_us + 999LL) / 1000LL));
        dtlsUnlock();
        vTaskDelay((wait_ticks > 0U) ? wait_ticks : 1U);
        if (!dtlsLock(portMAX_DELAY)) {
            ESP_LOGE(TAG, "DTLS mutex unavailable");
            return -1;
        }
        dtlsApplyPendingResetLocked();
        if (!(s_udp_enabled && s_dtls_configured)) {
            dtlsUnlock();
            return -1;
        }
    }

    s_dtls_send_ready_after_us = 0;

    for (int attempt = 0; attempt < UDP_SEND_RETRY_COUNT; attempt++) {
        ret = mbedtls_ssl_write(&s_dtls_ssl, (const unsigned char *)payload, len);
        if (ret > 0) {
            if ((size_t)ret != len) {
                ESP_LOGW(TAG, "mbedtls_ssl_write() partial send ret=%d expected=%u",
                         ret,
                         (unsigned int)len);
                dtlsSetRetryDelayMs(DTLS_RETRY_BACKOFF_MS);
                dtlsResetLocked();
                dtlsUnlock();
                return -1;
            }
            dtlsUnlock();
            return ret;
        }

        if ((ret == MBEDTLS_ERR_SSL_WANT_READ) || (ret == MBEDTLS_ERR_SSL_WANT_WRITE)) {
            dtlsUnlock();
            vTaskDelay(UDP_SEND_RETRY_DELAY_TICKS);
            if (!dtlsLock(portMAX_DELAY)) {
                ESP_LOGE(TAG, "DTLS mutex unavailable");
                return -1;
            }
            dtlsApplyPendingResetLocked();
            if (!(s_udp_enabled && s_dtls_configured)) {
                dtlsUnlock();
                return -1;
            }
            continue;
        }

        if ((ret == MBEDTLS_ERR_NET_SEND_FAILED) && (errno == ENOMEM)) {
            if (attempt + 1 >= UDP_SEND_RETRY_COUNT) {
                s_dtls_tx_congested_until_us = esp_timer_get_time() + DTLS_TX_CONGESTION_HOLD_US;
                dtlsLogError("mbedtls_ssl_write() failed after ENOMEM retries", ret);
                dtlsUnlock();
                return -1;
            }

            const TickType_t enomem_delay_ticks =
                UDP_SEND_ENOMEM_BASE_DELAY_TICKS * (TickType_t)(attempt + 1);
            s_dtls_tx_congested_until_us = esp_timer_get_time() + DTLS_TX_CONGESTION_HOLD_US;
            dtlsUnlock();
            vTaskDelay((enomem_delay_ticks > 0U)
                           ? enomem_delay_ticks
                           : UDP_SEND_RETRY_DELAY_TICKS);
            if (!dtlsLock(portMAX_DELAY)) {
                ESP_LOGE(TAG, "DTLS mutex unavailable");
                return -1;
            }
            dtlsApplyPendingResetLocked();
            if (!(s_udp_enabled && s_dtls_configured)) {
                dtlsUnlock();
                return -1;
            }
            continue;
        }

        dtlsLogError("mbedtls_ssl_write() failed", ret);

        if ((ret == MBEDTLS_ERR_SSL_TIMEOUT)
#ifdef MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY
            || (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
#endif
           ) {
            dtlsSetRetryDelayMs(DTLS_RETRY_BACKOFF_MS);
            dtlsResetLocked();
            dtlsUnlock();
            return -1;
        }

        dtlsSetRetryDelayMs(DTLS_RETRY_BACKOFF_MS);
        dtlsResetLocked();
        dtlsUnlock();
        return -1;
    }

    return -1;
}

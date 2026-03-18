#include "udp_stream.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <errno.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#if defined(__has_include)
#if __has_include("../app_secrets.h")
#include "../app_secrets.h"
#else
#include "../app_secrets.defaults.h"
#endif
#else
#include "../app_secrets.defaults.h"
#endif

static const char *TAG = "udp_stream";
static const TickType_t UDP_SEND_RETRY_DELAY_TICKS = 1U;
static const int UDP_SEND_RETRY_COUNT = 3;
<<<<<<< Updated upstream
=======
static const uint32_t DTLS_HANDSHAKE_MIN_MS = 1000U;
static const uint32_t DTLS_HANDSHAKE_MAX_MS = 8000U;
static const uint32_t DTLS_READ_TIMEOUT_MS = 1000U;
static const uint32_t DTLS_RETRY_BACKOFF_MS = 10000U;
#define DTLS_MAX_PSK_BYTES 64U
#define DTLS_ERRBUF_LEN 128U

static mbedtls_net_context s_dtls_net;
static mbedtls_ssl_context s_dtls_ssl;
static mbedtls_ssl_config s_dtls_conf;
static mbedtls_ctr_drbg_context s_dtls_ctr_drbg;
static mbedtls_entropy_context s_dtls_entropy;
static mbedtls_timing_delay_context s_dtls_timer;
>>>>>>> Stashed changes

static int s_udp_sock = -1;
static bool s_udp_enabled = false;
<<<<<<< Updated upstream
static bool s_udp_init_attempted = false;
static struct sockaddr_in s_udp_addr = {0};
=======
static bool s_dtls_context_initialized = false;
static bool s_dtls_configured = false;
static int64_t s_dtls_retry_after_us = 0;

static void dtlsLogError(const char *message, int err)
{
    char errbuf[DTLS_ERRBUF_LEN] = {0};
    mbedtls_strerror(err, errbuf, sizeof(errbuf));
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
>>>>>>> Stashed changes

void udpStreamReset(void)
{
    if (s_udp_sock >= 0) {
        close(s_udp_sock);
    }

    s_udp_sock = -1;
    s_udp_enabled = false;
    s_udp_init_attempted = false;
    memset(&s_udp_addr, 0, sizeof(s_udp_addr));
}

void udpStreamDeferInit(uint32_t delay_ms)
{
    dtlsSetRetryDelayMs(delay_ms);
}

static bool udpStreamConfigValid(void)
{
    return (strlen(APP_UDP_TARGET_IP) > 0U) && (APP_UDP_TARGET_PORT > 0);
}

bool udpStreamIsEnabled(void)
{
    return udpStreamConfigValid();
}

bool udpStreamIsReady(void)
{
    return s_udp_enabled && (s_udp_sock >= 0);
}

bool udpStreamInit(void)
{
<<<<<<< Updated upstream
    if (s_udp_init_attempted && !udpStreamIsReady()) {
        return false;
    }
=======
    const int64_t now_us = esp_timer_get_time();
>>>>>>> Stashed changes

    if (udpStreamIsReady()) {
        return true;
    }

    s_udp_init_attempted = true;

    if (!udpStreamConfigValid()) {
        ESP_LOGI(TAG, "UDP stream disabled (APP_UDP_TARGET_IP not configured)");
        return false;
    }

<<<<<<< Updated upstream
    s_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_udp_sock < 0) {
        ESP_LOGE(TAG, "socket() failed errno=%d", errno);
        return false;
    }

    memset(&s_udp_addr, 0, sizeof(s_udp_addr));
    s_udp_addr.sin_family = AF_INET;
    s_udp_addr.sin_port = htons((uint16_t)APP_UDP_TARGET_PORT);

    if (inet_aton(APP_UDP_TARGET_IP, &s_udp_addr.sin_addr) == 0) {
        ESP_LOGE(TAG, "invalid APP_UDP_TARGET_IP: %s", APP_UDP_TARGET_IP);
=======
    if ((s_dtls_retry_after_us != 0) && (now_us < s_dtls_retry_after_us)) {
        return false;
    }

    udpStreamReset();
    dtlsLogHeap("DTLS init begin");

    if (dtlsConfigureClient() != 0) {
        dtlsSetRetryDelayMs(DTLS_RETRY_BACKOFF_MS);
        dtlsLogHeap("DTLS init failed");
>>>>>>> Stashed changes
        udpStreamReset();
        return false;
    }

<<<<<<< Updated upstream
    s_udp_enabled = true;
    ESP_LOGI(TAG, "UDP stream target=%s:%d", APP_UDP_TARGET_IP, (int)APP_UDP_TARGET_PORT);
=======
    {
        const int handshake_ret = dtlsPerformHandshake();
        if (handshake_ret != 0) {
            dtlsLogError("DTLS handshake failed", handshake_ret);
            dtlsSetRetryDelayMs(DTLS_RETRY_BACKOFF_MS);
            dtlsLogHeap("DTLS handshake failed");
            udpStreamReset();
            return false;
        }
    }

    s_dtls_retry_after_us = 0;
    s_dtls_configured = true;
    s_udp_enabled = true;
    dtlsLogHeap("DTLS init OK");
    ESP_LOGI(TAG,
             "DTLS stream target=%s:%d identity=%s",
             APP_DTLS_TARGET_HOST,
             (int)APP_DTLS_TARGET_PORT,
             APP_DTLS_PSK_IDENTITY);
>>>>>>> Stashed changes
    return true;
}

int udpStreamSend(const void *payload, size_t len)
{
    int sent = -1;
    int last_errno = 0;

    if ((payload == NULL) || (len == 0U)) {
        return -1;
    }

    if (!udpStreamIsReady() && !udpStreamInit()) {
        return -1;
    }

    for (int attempt = 0; attempt < UDP_SEND_RETRY_COUNT; attempt++) {
        sent = sendto(s_udp_sock,
                      payload,
                      len,
                      0,
                      (const struct sockaddr *)&s_udp_addr,
                      sizeof(s_udp_addr));
        if (sent >= 0) {
            return sent;
        }

<<<<<<< Updated upstream
        last_errno = errno;
        if ((last_errno == ENETDOWN) || (last_errno == ENETUNREACH) || (last_errno == EHOSTUNREACH)) {
=======
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
            dtlsSetRetryDelayMs(DTLS_RETRY_BACKOFF_MS);
>>>>>>> Stashed changes
            udpStreamReset();
            break;
        }
        if (last_errno != ENOMEM) {
            break;
        }

        vTaskDelay(UDP_SEND_RETRY_DELAY_TICKS);
    }

    if (sent < 0) {
        ESP_LOGW(TAG, "sendto() failed errno=%d", last_errno);
    }

    return sent;
}

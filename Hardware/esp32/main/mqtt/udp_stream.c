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
#include "lwip/netdb.h"
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

#ifndef APP_UDP_TARGET_HOST
#define APP_UDP_TARGET_HOST ""
#endif

#ifndef APP_UDP_TARGET_PORT
#define APP_UDP_TARGET_PORT 5005
#endif

static const char *TAG = "udp_stream";
static const TickType_t UDP_SEND_RETRY_DELAY_TICKS = 1U;
static const int UDP_SEND_RETRY_COUNT = 8;
static const TickType_t UDP_SEND_ENOMEM_BASE_DELAY_TICKS = pdMS_TO_TICKS(8U);
static const TickType_t UDP_SEND_POST_CONNECT_DELAY_TICKS = pdMS_TO_TICKS(20U);
static const int64_t UDP_TX_CONGESTION_HOLD_US = 50000LL;
static const uint32_t UDP_RETRY_BACKOFF_MS = 3000U;

static int s_udp_sock = -1;
static SemaphoreHandle_t s_udp_mutex = NULL;

static bool s_udp_enabled = false;
static bool s_udp_connected = false;
static bool s_udp_ever_connected = false;
static volatile bool s_udp_reset_requested = false;
static int64_t s_udp_send_ready_after_us = 0;
static int64_t s_udp_tx_congested_until_us = 0;
static int64_t s_udp_retry_after_us = 0;

static void udpApplyPendingResetLocked(void);

static void udpLogHeap(const char *message)
{
    ESP_LOGI(TAG,
             "%s free_heap=%lu min_heap=%lu largest_8bit=%lu",
             message,
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static bool udpEnsureMutex(void)
{
    if (s_udp_mutex != NULL) {
        return true;
    }

    s_udp_mutex = xSemaphoreCreateMutex();
    return (s_udp_mutex != NULL);
}

static bool udpLock(TickType_t timeout_ticks)
{
    return udpEnsureMutex() &&
           (xSemaphoreTake(s_udp_mutex, timeout_ticks) == pdTRUE);
}

static void udpUnlock(void)
{
    if (s_udp_mutex != NULL) {
        (void)xSemaphoreGive(s_udp_mutex);
    }
}

static void udpSetRetryDelayMs(uint32_t delay_ms)
{
    if (delay_ms == 0U) {
        s_udp_retry_after_us = 0;
        return;
    }

    s_udp_retry_after_us = esp_timer_get_time() + ((int64_t)delay_ms * 1000LL);
}

static void udpCloseSocketLocked(void)
{
    if (s_udp_sock >= 0) {
        (void)lwip_close(s_udp_sock);
        s_udp_sock = -1;
    }
}

static void udpResetLocked(void)
{
    udpCloseSocketLocked();
    s_udp_enabled = false;
    s_udp_connected = false;
    s_udp_reset_requested = false;
    s_udp_send_ready_after_us = 0;
    s_udp_tx_congested_until_us = 0;
}

static void udpApplyPendingResetLocked(void)
{
    if (s_udp_reset_requested) {
        udpResetLocked();
    }
}

static bool udpDelayAndRelock(TickType_t delay_ticks)
{
    udpUnlock();
    vTaskDelay((delay_ticks > 0U) ? delay_ticks : 1U);

    if (!udpLock(portMAX_DELAY)) {
        ESP_LOGE(TAG, "UDP mutex unavailable");
        return false;
    }

    udpApplyPendingResetLocked();
    if (!(s_udp_enabled && s_udp_connected)) {
        udpUnlock();
        return false;
    }

    return true;
}

static bool udpWaitUntilLocked(int64_t wait_until_us)
{
    const int64_t now_us = esp_timer_get_time();

    if ((wait_until_us == 0) || (now_us >= wait_until_us)) {
        return true;
    }

    const int64_t wait_us = wait_until_us - now_us;
    const TickType_t wait_ticks = pdMS_TO_TICKS((uint32_t)((wait_us + 999LL) / 1000LL));
    return udpDelayAndRelock(wait_ticks);
}

static bool udpStreamConfigValid(void)
{
    return (strlen(APP_UDP_TARGET_HOST) > 0U) &&
           (APP_UDP_TARGET_PORT > 0);
}

bool udpStreamIsEnabled(void)
{
    return udpStreamConfigValid();
}

bool udpStreamHasConnectedOnce(void)
{
    bool connected_once = false;

    if (!udpLock(portMAX_DELAY)) {
        return false;
    }

    connected_once = s_udp_ever_connected;
    udpUnlock();
    return connected_once;
}

bool udpStreamIsCongested(void)
{
    bool congested = false;

    if (!udpLock(portMAX_DELAY)) {
        return false;
    }

    congested = (s_udp_tx_congested_until_us != 0) &&
                (esp_timer_get_time() < s_udp_tx_congested_until_us);
    udpUnlock();
    return congested;
}

bool udpStreamIsReady(void)
{
    bool ready = false;

    if (!udpLock(portMAX_DELAY)) {
        return false;
    }

    udpApplyPendingResetLocked();
    ready = s_udp_enabled && s_udp_connected;
    udpUnlock();
    return ready;
}

void udpStreamRequestReset(void)
{
    if (!udpLock(0U)) {
        s_udp_reset_requested = true;
        return;
    }

    s_udp_enabled = false;
    s_udp_connected = false;
    s_udp_reset_requested = true;
    udpUnlock();
}

void udpStreamReset(void)
{
    if (!udpLock(portMAX_DELAY)) {
        s_udp_enabled = false;
        s_udp_connected = false;
        s_udp_reset_requested = true;
        return;
    }

    udpResetLocked();
    udpUnlock();
}

void udpStreamDeferInit(uint32_t delay_ms)
{
    if (!udpLock(portMAX_DELAY)) {
        udpSetRetryDelayMs(delay_ms);
        return;
    }

    udpSetRetryDelayMs(delay_ms);
    udpUnlock();
}

static int udpConnectSocketLocked(void)
{
    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;
    char port_str[8];
    int ret = -1;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    (void)snprintf(port_str, sizeof(port_str), "%d", (int)APP_UDP_TARGET_PORT);

    if (lwip_getaddrinfo(APP_UDP_TARGET_HOST, port_str, &hints, &res) != 0) {
        ESP_LOGW(TAG, "lwip_getaddrinfo() failed host=%s port=%s", APP_UDP_TARGET_HOST, port_str);
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        s_udp_sock = lwip_socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s_udp_sock < 0) {
            continue;
        }

        if (lwip_connect(s_udp_sock, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) {
            ret = 0;
            break;
        }

        udpCloseSocketLocked();
    }

    lwip_freeaddrinfo(res);
    return ret;
}

bool udpStreamInit(void)
{
    bool ok = false;

    if (!udpLock(portMAX_DELAY)) {
        ESP_LOGE(TAG, "UDP mutex unavailable");
        return false;
    }

    udpApplyPendingResetLocked();

    if (s_udp_enabled && s_udp_connected) {
        udpUnlock();
        return true;
    }

    if (!udpStreamConfigValid()) {
        ESP_LOGI(TAG, "UDP stream disabled (target host/port not configured)");
        udpUnlock();
        return false;
    }

    if ((s_udp_retry_after_us != 0) && (esp_timer_get_time() < s_udp_retry_after_us)) {
        udpUnlock();
        return false;
    }

    udpResetLocked();
    udpLogHeap("UDP init begin");

    if (udpConnectSocketLocked() != 0) {
        udpSetRetryDelayMs(UDP_RETRY_BACKOFF_MS);
        udpLogHeap("UDP init failed");
        udpResetLocked();
        udpUnlock();
        return false;
    }

    s_udp_retry_after_us = 0;
    s_udp_enabled = true;
    s_udp_connected = true;
    s_udp_ever_connected = true;
    s_udp_send_ready_after_us = esp_timer_get_time() +
                                ((int64_t)UDP_SEND_POST_CONNECT_DELAY_TICKS * 1000LL *
                                 portTICK_PERIOD_MS);
    udpLogHeap("UDP init OK");
    ESP_LOGI(TAG, "UDP stream target=%s:%d", APP_UDP_TARGET_HOST, (int)APP_UDP_TARGET_PORT);
    ok = true;
    udpUnlock();
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

    if (!udpLock(portMAX_DELAY)) {
        ESP_LOGE(TAG, "UDP mutex unavailable");
        return -1;
    }

    udpApplyPendingResetLocked();
    if (!(s_udp_enabled && s_udp_connected)) {
        udpUnlock();
        return -1;
    }

    for (int attempt = 0; attempt < UDP_SEND_RETRY_COUNT; attempt++) {
        int64_t wait_until_us = s_udp_send_ready_after_us;

        if (s_udp_tx_congested_until_us > wait_until_us) {
            wait_until_us = s_udp_tx_congested_until_us;
        }

        if (!udpWaitUntilLocked(wait_until_us)) {
            return -1;
        }

        s_udp_send_ready_after_us = 0;
        if ((s_udp_tx_congested_until_us != 0) &&
            (esp_timer_get_time() >= s_udp_tx_congested_until_us)) {
            s_udp_tx_congested_until_us = 0;
        }

        ret = (int)lwip_send(s_udp_sock, payload, len, 0);
        if (ret >= 0) {
            if ((size_t)ret != len) {
                ESP_LOGW(TAG, "lwip_send() partial send ret=%d expected=%u",
                         ret,
                         (unsigned int)len);
                udpSetRetryDelayMs(UDP_RETRY_BACKOFF_MS);
                udpResetLocked();
                udpUnlock();
                return -1;
            }

            s_udp_tx_congested_until_us = 0;
            udpUnlock();
            return ret;
        }

        const int saved_errno = errno;
        if ((saved_errno == EAGAIN) || (saved_errno == EWOULDBLOCK) || (saved_errno == ENOMEM)) {
            s_udp_tx_congested_until_us = esp_timer_get_time() + UDP_TX_CONGESTION_HOLD_US;

            if (attempt + 1 >= UDP_SEND_RETRY_COUNT) {
                ESP_LOGW(TAG,
                         "lwip_send() failed after congestion retries errno=%d (%s)",
                         saved_errno,
                         strerror(saved_errno));
                udpLogHeap("UDP send congestion");
                udpUnlock();
                return -1;
            }

            const TickType_t delay_ticks =
                (saved_errno == ENOMEM)
                    ? (UDP_SEND_ENOMEM_BASE_DELAY_TICKS * (TickType_t)(attempt + 1))
                    : UDP_SEND_RETRY_DELAY_TICKS;

            if (!udpDelayAndRelock(delay_ticks)) {
                return -1;
            }
            continue;
        }

        ESP_LOGW(TAG, "lwip_send() failed errno=%d (%s)", saved_errno, strerror(saved_errno));
        udpSetRetryDelayMs(UDP_RETRY_BACKOFF_MS);
        udpResetLocked();
        udpUnlock();
        return -1;
    }

    return -1;
}

#include "udp_stream.h"

#include "esp_log.h"
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

static int s_udp_sock = -1;
static bool s_udp_enabled = false;
static bool s_udp_init_attempted = false;
static struct sockaddr_in s_udp_addr = {0};

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
    if (s_udp_init_attempted && !udpStreamIsReady()) {
        return false;
    }

    if (udpStreamIsReady()) {
        return true;
    }

    s_udp_init_attempted = true;

    if (!udpStreamConfigValid()) {
        ESP_LOGI(TAG, "UDP stream disabled (APP_UDP_TARGET_IP not configured)");
        return false;
    }

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
        udpStreamReset();
        return false;
    }

    s_udp_enabled = true;
    ESP_LOGI(TAG, "UDP stream target=%s:%d", APP_UDP_TARGET_IP, (int)APP_UDP_TARGET_PORT);
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

        last_errno = errno;
        if ((last_errno == ENETDOWN) || (last_errno == ENETUNREACH) || (last_errno == EHOSTUNREACH)) {
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

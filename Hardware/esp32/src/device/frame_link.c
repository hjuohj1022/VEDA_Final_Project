#include "frame_link.h"

#include "driver/spi_slave.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_SPI_HOST          SPI2_HOST
#define FRAME_SPI_PIN_SCK       4
#define FRAME_SPI_PIN_MISO      5
#define FRAME_SPI_PIN_MOSI      6
#define FRAME_SPI_PIN_CS        7

#define FRAME_SPI_PKT_SIZE      256U
#define FRAME_SPI_HEADER_SIZE   (14U)
#define FRAME_SPI_PAYLOAD_SIZE  (FRAME_SPI_PKT_SIZE - FRAME_SPI_HEADER_SIZE)

static uint8_t *s_frame_bufs[NUM_BUFFERS] = {NULL};
static uint16_t s_frame_ids[NUM_BUFFERS] = {0U};
static uint8_t s_allocated_buffers = 0U;

static volatile uint8_t s_frame_ready = 0U;
static volatile int s_write_idx = 0;
static volatile int s_read_idx = -1;
static SemaphoreHandle_t s_frame_mutex = NULL;

static uint8_t *s_rx_buf = NULL;
static uint8_t *s_tx_buf = NULL;

static uint32_t s_total_packets = 0U;
static uint32_t s_completed_frames = 0U;
static uint32_t s_spi_timeouts = 0U;
static uint32_t s_spi_errors = 0U;
static uint32_t s_bad_magic = 0U;
static uint32_t s_bad_checksum = 0U;
static uint32_t s_bad_payload_len = 0U;
static uint32_t s_seq_errors = 0U;
static uint32_t s_queue_full_drops = 0U;
static bool s_waiting_for_first_packet_logged = false;

static void logFrameLinkSummary(void)
{
    (void)printf("SPI frame link: packets=%lu frames=%lu timeouts=%lu errors=%lu "
                 "bad_magic=%lu bad_checksum=%lu bad_len=%lu seq=%lu queue_full=%lu ready=%u\n",
                 (unsigned long)s_total_packets,
                 (unsigned long)s_completed_frames,
                 (unsigned long)s_spi_timeouts,
                 (unsigned long)s_spi_errors,
                 (unsigned long)s_bad_magic,
                 (unsigned long)s_bad_checksum,
                 (unsigned long)s_bad_payload_len,
                 (unsigned long)s_seq_errors,
                 (unsigned long)s_queue_full_drops,
                 (unsigned int)s_frame_ready);
}

static uint16_t calcChecksum(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0U;

    for (uint16_t i = 0U; i < len; i++) {
        sum += data[i];
    }

    return (uint16_t)(sum & 0xFFFFU);
}

static uint16_t readU16Be(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8U) | (uint16_t)src[1]);
}

static bool queueIsFull(void)
{
    bool is_full = false;

    if ((s_frame_mutex != NULL) &&
        (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(20U)) == pdTRUE)) {
        is_full = (s_frame_ready >= s_allocated_buffers) && (s_allocated_buffers > 0U);
        (void)xSemaphoreGive(s_frame_mutex);
    }

    return is_full;
}

bool frameLinkAcquireReadyFrame(const uint8_t **frame_buf, uint16_t *frame_id, int *buffer_idx)
{
    bool ok = false;

    if ((frame_buf == NULL) || (frame_id == NULL) || (buffer_idx == NULL)) {
        return false;
    }

    if ((s_frame_mutex != NULL) &&
        (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(20U)) == pdTRUE)) {
        if ((s_frame_ready > 0U) && (s_read_idx >= 0) && (s_read_idx < (int)s_allocated_buffers)) {
            *buffer_idx = s_read_idx;
            *frame_buf = s_frame_bufs[s_read_idx];
            *frame_id = s_frame_ids[s_read_idx];
            ok = true;
        }
        (void)xSemaphoreGive(s_frame_mutex);
    }

    return ok;
}

void frameLinkReleaseReadyFrame(int buffer_idx)
{
    if ((s_frame_mutex == NULL) || (buffer_idx < 0)) {
        return;
    }

    if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(20U)) == pdTRUE) {
        if ((s_frame_ready > 0U) &&
            (s_read_idx == buffer_idx) &&
            (s_allocated_buffers > 0U)) {
            s_frame_ready--;
            if (s_frame_ready > 0U) {
                s_read_idx = (s_read_idx + 1) % (int)s_allocated_buffers;
            } else {
                s_read_idx = -1;
            }
        }
        (void)xSemaphoreGive(s_frame_mutex);
    }
}

void frameLinkGetStats(frame_link_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    stats->total_packets = s_total_packets;
    stats->completed_frames = s_completed_frames;
    stats->spi_timeouts = s_spi_timeouts;
    stats->spi_errors = s_spi_errors;
    stats->bad_magic = s_bad_magic;
    stats->bad_checksum = s_bad_checksum;
    stats->bad_payload_len = s_bad_payload_len;
    stats->seq_errors = s_seq_errors;
    stats->queue_full_drops = s_queue_full_drops;
    stats->frame_ready = s_frame_ready;
}

void frameLinkInit(void)
{
    for (uint8_t i = 0U; i < NUM_BUFFERS; i++) {
        s_frame_bufs[i] = (uint8_t *)malloc((size_t)FRAME_BYTES);
        if (s_frame_bufs[i] == NULL) {
            break;
        }
        s_allocated_buffers++;
    }

    s_rx_buf = (uint8_t *)heap_caps_malloc(FRAME_SPI_PKT_SIZE, MALLOC_CAP_DMA);
    s_tx_buf = (uint8_t *)heap_caps_calloc(FRAME_SPI_PKT_SIZE, 1U, MALLOC_CAP_DMA);

    if ((s_allocated_buffers == 0U) || (s_rx_buf == NULL) || (s_tx_buf == NULL)) {
        (void)printf("Frame link allocation failed\n");
        return;
    }

    s_frame_mutex = xSemaphoreCreateMutex();
    if (s_frame_mutex == NULL) {
        (void)printf("Frame link mutex create failed\n");
        return;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = FRAME_SPI_PIN_MOSI,
        .miso_io_num = FRAME_SPI_PIN_MISO,
        .sclk_io_num = FRAME_SPI_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FRAME_SPI_PKT_SIZE,
    };

    spi_slave_interface_config_t slave_cfg = {
        .mode = 0,
        .spics_io_num = FRAME_SPI_PIN_CS,
        .queue_size = 4,
        .flags = 0,
    };

    ESP_ERROR_CHECK(spi_slave_initialize(FRAME_SPI_HOST, &bus_cfg, &slave_cfg, SPI_DMA_CH_AUTO));

    (void)printf("Frame link SPI slave init OK (pins sck=%d miso=%d mosi=%d cs=%d, frame_bufs=%u)\n",
                 FRAME_SPI_PIN_SCK,
                 FRAME_SPI_PIN_MISO,
                 FRAME_SPI_PIN_MOSI,
                 FRAME_SPI_PIN_CS,
                 (unsigned int)s_allocated_buffers);
    (void)printf("Heap after frame link init: free=%lu min=%lu\n",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
}

void frameLinkTask(void *arg)
{
    bool assembly_active = false;
    uint16_t expected_frame_id = 0U;
    uint16_t expected_chunk_idx = 0U;
    uint16_t expected_total_chunks = 0U;
    uint32_t assembled_bytes = 0U;
    int target_idx = 0;
    TickType_t last_summary_tick = xTaskGetTickCount();
    uint32_t last_summary_packets = 0U;
    uint32_t last_summary_frames = 0U;
    uint32_t last_summary_timeouts = 0U;
    uint32_t last_summary_errors = 0U;
    uint32_t last_summary_bad_magic = 0U;
    uint32_t last_summary_bad_checksum = 0U;
    uint32_t last_summary_bad_len = 0U;
    uint32_t last_summary_seq = 0U;
    uint32_t last_summary_queue_full = 0U;
    (void)arg;

    for (;;) {
        spi_slave_transaction_t trans = {
            .length = FRAME_SPI_PKT_SIZE * 8U,
            .tx_buffer = s_tx_buf,
            .rx_buffer = s_rx_buf,
        };
        uint16_t frame_id = 0U;
        uint16_t chunk_idx = 0U;
        uint16_t total_chunks = 0U;
        uint16_t payload_len = 0U;
        uint16_t checksum = 0U;
        const uint8_t *payload;

        if ((s_allocated_buffers == 0U) || (s_rx_buf == NULL) || (s_tx_buf == NULL)) {
            vTaskDelay(pdMS_TO_TICKS(100U));
            continue;
        }

        const esp_err_t ret = spi_slave_transmit(FRAME_SPI_HOST, &trans, pdMS_TO_TICKS(1000U));
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_TIMEOUT) {
                s_spi_timeouts++;
            } else {
                s_spi_errors++;
                (void)printf("Frame link SPI receive failed: %s\n", esp_err_to_name(ret));
            }
            if ((xTaskGetTickCount() - last_summary_tick) >= pdMS_TO_TICKS(2000U)) {
                if ((s_total_packets == 0U) &&
                    (s_spi_errors == 0U) &&
                    (s_bad_magic == 0U) &&
                    (s_bad_checksum == 0U) &&
                    (s_bad_payload_len == 0U) &&
                    (s_seq_errors == 0U) &&
                    (s_queue_full_drops == 0U)) {
                    if (!s_waiting_for_first_packet_logged) {
                        (void)printf("SPI frame link idle: waiting for first SPI packet\n");
                        s_waiting_for_first_packet_logged = true;
                    }
                } else if ((s_total_packets != last_summary_packets) ||
                           (s_completed_frames != last_summary_frames) ||
                           (s_spi_timeouts != last_summary_timeouts) ||
                           (s_spi_errors != last_summary_errors) ||
                           (s_bad_magic != last_summary_bad_magic) ||
                           (s_bad_checksum != last_summary_bad_checksum) ||
                           (s_bad_payload_len != last_summary_bad_len) ||
                           (s_seq_errors != last_summary_seq) ||
                           (s_queue_full_drops != last_summary_queue_full)) {
                    logFrameLinkSummary();
                    last_summary_packets = s_total_packets;
                    last_summary_frames = s_completed_frames;
                    last_summary_timeouts = s_spi_timeouts;
                    last_summary_errors = s_spi_errors;
                    last_summary_bad_magic = s_bad_magic;
                    last_summary_bad_checksum = s_bad_checksum;
                    last_summary_bad_len = s_bad_payload_len;
                    last_summary_seq = s_seq_errors;
                    last_summary_queue_full = s_queue_full_drops;
                }
                last_summary_tick = xTaskGetTickCount();
            }
            continue;
        }

        s_total_packets++;
        s_waiting_for_first_packet_logged = false;
        payload = &s_rx_buf[FRAME_SPI_HEADER_SIZE];

        if ((s_rx_buf[0] != (uint8_t)'T') ||
            (s_rx_buf[1] != (uint8_t)'E') ||
            (s_rx_buf[2] != (uint8_t)'S') ||
            (s_rx_buf[3] != (uint8_t)'T')) {
            s_bad_magic++;
            assembly_active = false;
            expected_chunk_idx = 0U;
            continue;
        }

        frame_id = readU16Be(&s_rx_buf[4]);
        chunk_idx = readU16Be(&s_rx_buf[6]);
        total_chunks = readU16Be(&s_rx_buf[8]);
        payload_len = readU16Be(&s_rx_buf[10]);
        checksum = readU16Be(&s_rx_buf[12]);

        if (payload_len > FRAME_SPI_PAYLOAD_SIZE) {
            s_bad_payload_len++;
            assembly_active = false;
            expected_chunk_idx = 0U;
            continue;
        }

        if (calcChecksum(payload, payload_len) != checksum) {
            s_bad_checksum++;
            assembly_active = false;
            expected_chunk_idx = 0U;
            continue;
        }

        if (chunk_idx == 0U) {
            if (queueIsFull()) {
                s_queue_full_drops++;
                assembly_active = false;
                expected_chunk_idx = 0U;
                continue;
            }

            target_idx = s_write_idx;
            expected_frame_id = frame_id;
            expected_total_chunks = total_chunks;
            expected_chunk_idx = 0U;
            assembled_bytes = 0U;
            assembly_active = true;
        }

        if ((!assembly_active) ||
            (frame_id != expected_frame_id) ||
            (chunk_idx != expected_chunk_idx) ||
            (total_chunks != expected_total_chunks)) {
            s_seq_errors++;
            assembly_active = false;
            expected_chunk_idx = 0U;
            continue;
        }

        const size_t offset = (size_t)chunk_idx * (size_t)FRAME_SPI_PAYLOAD_SIZE;
        if ((offset + payload_len) > FRAME_BYTES) {
            s_bad_payload_len++;
            assembly_active = false;
            expected_chunk_idx = 0U;
            continue;
        }

        (void)memcpy(&s_frame_bufs[target_idx][offset], payload, payload_len);
        assembled_bytes += payload_len;
        expected_chunk_idx++;

        if (expected_chunk_idx >= expected_total_chunks) {
            if ((assembled_bytes == FRAME_BYTES) &&
                (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(20U)) == pdTRUE)) {
                s_frame_ids[target_idx] = expected_frame_id;
                s_completed_frames++;

                if (s_frame_ready == 0U) {
                    s_read_idx = target_idx;
                }

                if (s_frame_ready < s_allocated_buffers) {
                    s_frame_ready++;
                }

                s_write_idx = (target_idx + 1) % (int)s_allocated_buffers;
                (void)xSemaphoreGive(s_frame_mutex);

                (void)printf(">> SPI frame captured id=%u queued=%u/%u\n",
                             (unsigned int)expected_frame_id,
                             (unsigned int)s_frame_ready,
                             (unsigned int)s_allocated_buffers);
            }

            assembly_active = false;
            expected_chunk_idx = 0U;
        }

        if ((xTaskGetTickCount() - last_summary_tick) >= pdMS_TO_TICKS(2000U)) {
            if ((s_total_packets != last_summary_packets) ||
                (s_completed_frames != last_summary_frames) ||
                (s_spi_timeouts != last_summary_timeouts) ||
                (s_spi_errors != last_summary_errors) ||
                (s_bad_magic != last_summary_bad_magic) ||
                (s_bad_checksum != last_summary_bad_checksum) ||
                (s_bad_payload_len != last_summary_bad_len) ||
                (s_seq_errors != last_summary_seq) ||
                (s_queue_full_drops != last_summary_queue_full)) {
                logFrameLinkSummary();
                last_summary_packets = s_total_packets;
                last_summary_frames = s_completed_frames;
                last_summary_timeouts = s_spi_timeouts;
                last_summary_errors = s_spi_errors;
                last_summary_bad_magic = s_bad_magic;
                last_summary_bad_checksum = s_bad_checksum;
                last_summary_bad_len = s_bad_payload_len;
                last_summary_seq = s_seq_errors;
                last_summary_queue_full = s_queue_full_drops;
            }
            last_summary_tick = xTaskGetTickCount();
        }
    }
}

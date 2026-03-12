#ifndef FRAME_LINK_H
#define FRAME_LINK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stdint.h>

#define FRAME_BYTES        38400U
#define NUM_BUFFERS        2U

typedef struct {
    uint32_t total_packets;
    uint32_t completed_frames;
    uint32_t spi_timeouts;
    uint32_t spi_errors;
    uint32_t bad_magic;
    uint32_t bad_checksum;
    uint32_t bad_payload_len;
    uint32_t seq_errors;
    uint32_t queue_full_drops;
    uint8_t frame_ready;
} frame_link_stats_t;

void frameLinkInit(void);
void frameLinkTask(void *arg);
bool frameLinkAcquireReadyFrame(const uint8_t **frame_buf, uint16_t *frame_id, int *buffer_idx);
void frameLinkReleaseReadyFrame(int buffer_idx);
void frameLinkGetStats(frame_link_stats_t *stats);

#endif

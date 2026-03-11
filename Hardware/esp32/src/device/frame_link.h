#ifndef FRAME_LINK_H
#define FRAME_LINK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stdint.h>

#define FRAME_BYTES        38400U
#define NUM_BUFFERS        2U

void frameLinkInit(void);
void frameLinkTask(void *arg);
bool frameLinkAcquireReadyFrame(const uint8_t **frame_buf, uint16_t *frame_id, int *buffer_idx);
void frameLinkReleaseReadyFrame(int buffer_idx);

#endif

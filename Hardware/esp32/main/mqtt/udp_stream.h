#ifndef UDP_STREAM_H
#define UDP_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool udpStreamInit(void);
bool udpStreamIsEnabled(void);
bool udpStreamIsCongested(void);
bool udpStreamHasConnectedOnce(void);
bool udpStreamIsReady(void);
void udpStreamDeferInit(uint32_t delay_ms);
void udpStreamRequestReset(void);
void udpStreamReset(void);
int udpStreamSend(const void *payload, size_t len);

#endif

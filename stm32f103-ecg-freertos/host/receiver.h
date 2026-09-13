#ifndef ECG_RECEIVER_H
#define ECG_RECEIVER_H
#include "protocol.h"
#define VIEW_SAMPLES 2000u
typedef void (*sample_sink_t)(void *, uint32_t, const uint16_t[3]);
typedef struct {
    uint32_t sequence;
    uint16_t values[3];
    uint8_t valid;
} received_sample_t;
typedef struct {
    received_sample_t samples[VIEW_SAMPLES];
    uint32_t session, floor, next, received, expired, duplicates;
    uint32_t adc_faults, rx_errors, diagnostics[12];
    uint32_t request_sequence, request_time;
    uint16_t request_count;
    uint8_t started;
} receiver_t;
void receiver_init(receiver_t *receiver, uint32_t session);
void receiver_accept(receiver_t *receiver, const packet_t *packet, sample_sink_t sink, void *context);
int receiver_get(const receiver_t *receiver, uint32_t seq, uint16_t values[3]);
uint32_t receiver_missing(const receiver_t *receiver);
int receiver_request(receiver_t *receiver, uint32_t now, packet_t *request);
#endif

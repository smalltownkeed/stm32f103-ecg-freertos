#include "receiver.h"
#include <string.h>

void receiver_init(receiver_t *receiver, uint32_t session)
{
    memset(receiver, 0, sizeof(*receiver));
    receiver->session = session;
}

int receiver_get(const receiver_t *receiver, uint32_t seq, uint16_t values[3])
{
    const received_sample_t *sample = &receiver->samples[seq % VIEW_SAMPLES];
    if (seq < receiver->floor || seq >= receiver->next || !sample->valid || sample->sequence != seq) return 0;
    if (values) memcpy(values, sample->values, 6);
    return 1;
}

static void advance_floor(receiver_t *receiver, uint32_t floor)
{
    if (floor <= receiver->floor) return;
    uint32_t saved = 0;
    for (unsigned i = 0; i < VIEW_SAMPLES; i++) {
        received_sample_t *sample = &receiver->samples[i];
        if (sample->valid && sample->sequence >= receiver->floor && sample->sequence < floor) {
            saved++;
            sample->valid = 0;
        }
    }
    receiver->expired += floor - receiver->floor - saved;
    receiver->floor = floor;
}

void receiver_accept(receiver_t *receiver, const packet_t *packet, sample_sink_t sink, void *context)
{
    if (packet->session != receiver->session) return;
    if (packet->type == PKT_STATUS && packet->size == 20) {
        uint32_t oldest = read_u32(packet->payload + 4);
        uint32_t next = read_u32(packet->payload + 8);
        if (read_u16(packet->payload) != 250 || read_u16(packet->payload + 2) != 3 || oldest > next) return;
        if (!receiver->started) {
            receiver->floor = next;
            receiver->next = next;
            receiver->started = 1;
        }
        if (next > receiver->next) receiver->next = next;
        receiver->adc_faults = read_u32(packet->payload + 12);
        receiver->rx_errors = read_u32(packet->payload + 16);
        advance_floor(receiver, oldest);
    } else if (packet->type == PKT_DATA && receiver->started) {
        if (!packet->count || packet->count > 25 || packet->size != packet->count * 6 ||
            packet->sequence > UINT32_MAX - packet->count) return;
        uint32_t end = packet->sequence + packet->count;
        if (end > receiver->next) receiver->next = end;
        if (receiver->next > VIEW_SAMPLES) advance_floor(receiver, receiver->next - VIEW_SAMPLES);
        for (unsigned i = 0; i < packet->count; i++) {
            uint32_t seq = packet->sequence + i;
            if (seq < receiver->floor) continue;
            if (receiver_get(receiver, seq, NULL)) { receiver->duplicates++; continue; }
            received_sample_t *sample = &receiver->samples[seq % VIEW_SAMPLES];
            sample->sequence = seq;
            sample->valid = 1;
            for (unsigned c = 0; c < 3; c++) sample->values[c] = read_u16(packet->payload + i * 6 + c * 2);
            receiver->received++;
            if (sink) sink(context, seq, sample->values);
        }
    } else if (packet->type == PKT_GAP && packet->size == 8) {
        uint32_t end = read_u32(packet->payload + 4);
        if (end <= receiver->next) advance_floor(receiver, end);
    } else if (packet->type == PKT_DIAG && packet->size == 48) {
        for (unsigned i = 0; i < 12; i++) receiver->diagnostics[i] = read_u32(packet->payload + 4 * i);
    }
    if (receiver->next > VIEW_SAMPLES) advance_floor(receiver, receiver->next - VIEW_SAMPLES);
}

uint32_t receiver_missing(const receiver_t *receiver)
{
    uint32_t missing = 0;
    for (uint32_t seq = receiver->floor; seq < receiver->next; seq++) {
        if (!receiver_get(receiver, seq, NULL)) missing++;
    }
    return missing;
}

int receiver_request(receiver_t *receiver, uint32_t now, packet_t *request)
{
    if (!receiver->started) return 0;
    if (receiver->request_count) {
        uint32_t end = receiver->request_sequence + receiver->request_count;
        for (uint32_t seq = receiver->request_sequence; seq < end; seq++) {
            if (seq >= receiver->floor && !receiver_get(receiver, seq, NULL)) {
                if (now - receiver->request_time < 100) return 0;
                goto send_request;
            }
        }
        receiver->request_count = 0;
    }
    for (uint32_t seq = receiver->floor; seq < receiver->next; seq++) {
        if (!receiver_get(receiver, seq, NULL)) {
            receiver->request_sequence = seq;
            receiver->request_count = 1;
            while (receiver->request_count < 25 && seq + receiver->request_count < receiver->next &&
                   !receiver_get(receiver, seq + receiver->request_count, NULL)) receiver->request_count++;
            goto send_request;
        }
    }
    return 0;
send_request:
    memset(request, 0, sizeof(*request));
    request->type = CMD_READ;
    request->session = receiver->session;
    request->sequence = receiver->request_sequence;
    request->count = receiver->request_count;
    receiver->request_time = now;
    return 1;
}

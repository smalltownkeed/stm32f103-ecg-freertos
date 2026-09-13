#include "protocol.h"
#include "history.h"
#include "receiver.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#condition); exit(1); } } while (0)
static history_t history;
static receiver_t receiver;
static parser_t parser;
static unsigned seen[6000];

static void sample_received(void *context, uint32_t seq, const uint16_t values[3])
{
    (void)context;
    CHECK(seq < 6000);
    CHECK(values[0] == seq % 4096 && values[1] == seq * 3 % 4096 && values[2] == seq * 7 % 4096);
    CHECK(seen[seq]++ == 0);
}

static void deliver(uint32_t first, unsigned count)
{
    packet_t p = {0}, decoded;
    p.type = PKT_DATA; p.session = 123; p.sequence = first;
    for (unsigned i = 0; i < count; i++) {
        uint16_t values[3];
        if (history_get(&history, first + i, values) != 1) break;
        for (unsigned c = 0; c < 3; c++) write_u16(p.payload + i * 6 + c * 2, values[c]);
        p.count++;
    }
    if (!p.count) return;
    p.size = p.count * 6;
    uint8_t bytes[FRAME_CAPACITY];
    size_t size = packet_encode(&p, bytes);
    for (size_t i = 0; i < size; i++) {
        if (parser_push(&parser, bytes[i], &decoded)) receiver_accept(&receiver, &decoded, sample_received, NULL);
    }
}

static void test_protocol(void)
{
    CHECK(crc16((const uint8_t *)"123456789", 9) == 0x29b1);
    packet_t p = {0}, decoded;
    p.type = CMD_READ; p.session = 0x12345678; p.sequence = 99; p.count = 25;
    uint8_t bytes[FRAME_CAPACITY];
    CHECK(packet_encode(&p, bytes) == 18);
    CHECK(bytes[4] == 0x78 && bytes[7] == 0x12);
    parser_t stream = {0};
    bytes[5] ^= 1;
    for (unsigned i = 0; i < 18; i++) CHECK(!parser_push(&stream, bytes[i], &decoded));
    CHECK(stream.errors == 1);
    bytes[5] ^= 1;
    int found = 0;
    for (unsigned i = 0; i < 18; i++) found += parser_push(&stream, bytes[i], &decoded);
    CHECK(found == 1 && decoded.sequence == 99 && decoded.count == 25);
    p.count = 26; CHECK(packet_encode(&p, bytes) == 0);
    puts("PASS protocol: CRC, resynchronization, malformed frame, byte order");
}

static void test_history_and_average(void)
{
    uint16_t block[768], output[3];
    for (unsigned i = 0; i < 256; i++) { block[i*3] = 4095; block[i*3+1] = 0; block[i*3+2] = i & 1 ? 4095 : 0; }
    adc_average(block, output);
    CHECK(output[0] == 4095 && output[1] == 0 && output[2] == 2048);
    for (unsigned i = 0; i < 1700; i++) { output[0] = (uint16_t)i; history_append(&history, output); }
    CHECK(history_oldest(&history) == 100);
    CHECK(history_get(&history, 99, output) == -1);
    CHECK(history_get(&history, 1700, output) == 0);
    CHECK(history_get(&history, 1601, output) == 1 && output[0] == 1601);
    puts("PASS history: average, wrap, expiry and future reads");
}

static void test_recovery(void)
{
    memset(&history, 0, sizeof(history));
    receiver_init(&receiver, 123);
    packet_t status = {0};
    status.type = PKT_STATUS; status.session = 123; status.size = 20;
    write_u16(status.payload, 250); write_u16(status.payload + 2, 3);
    receiver_accept(&receiver, &status, NULL, NULL);
    for (uint32_t seq = 0; seq < 6000; seq++) {
        uint16_t values[3] = {(uint16_t)(seq%4096),(uint16_t)(seq*3%4096),(uint16_t)(seq*7%4096)};
        history_append(&history, values);
        int blackout = seq >= 1250 && seq < 2500;
        if (seq%25 == 24 && !blackout && seq%175 != 174) deliver(seq - 24, 25);
        if (seq%250 == 249 && !blackout) {
            write_u32(status.payload + 4, history_oldest(&history));
            write_u32(status.payload + 8, history.next);
            receiver_accept(&receiver, &status, NULL, NULL);
        }
        packet_t request;
        if (!blackout && receiver_request(&receiver, seq*4, &request) && seq%113 != 0) deliver(request.sequence, request.count);
    }
    for (unsigned i = 0; i < 1000; i++) {
        packet_t request;
        if (receiver_request(&receiver, 24000 + i*4, &request)) deliver(request.sequence, request.count);
    }
    CHECK(receiver.expired == 0 && receiver_missing(&receiver) == 0);
    CHECK(receiver.received == 6000);
    for (unsigned i = 0; i < 6000; i++) CHECK(seen[i] == 1);
    deliver(5975, 25);
    CHECK(receiver.received == 6000 && receiver.duplicates == 25);
    puts("PASS recovery: 5s outage + repeated packet loss, 6000 exact samples, deduplication");
}

static void test_session_and_expiry(void)
{
    receiver_init(&receiver, 456);
    packet_t status = {0};
    status.type = PKT_STATUS; status.session = 123; status.size = 20;
    write_u16(status.payload, 250); write_u16(status.payload+2, 3);
    receiver_accept(&receiver, &status, NULL, NULL);
    CHECK(!receiver.started);
    status.session = 456;
    receiver_accept(&receiver, &status, NULL, NULL);
    write_u32(status.payload+4, 1400); write_u32(status.payload+8, 3000);
    receiver_accept(&receiver, &status, NULL, NULL);
    CHECK(receiver.expired == 1400 && receiver_missing(&receiver) == 1600);
    packet_t request;
    CHECK(receiver_request(&receiver, 0, &request) && request.sequence == 1400);
    CHECK(!receiver_request(&receiver, 50, &request));
    CHECK(receiver_request(&receiver, 100, &request));
    puts("PASS session isolation, buffer expiration, bounded requests and timeout retry");
}

int main(void)
{
    test_protocol(); test_history_and_average(); test_recovery(); test_session_and_expiry();
    puts("ALL C TESTS PASSED");
    return 0;
}

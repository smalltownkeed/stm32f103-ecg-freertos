#ifndef ECG_PROTOCOL_H
#define ECG_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define ECG_SAMPLE_RATE 250u
#define ECG_CHANNELS 3u
#define PACKET_SAMPLES 25u
#define PAYLOAD_CAPACITY 150u
#define FRAME_CAPACITY 168u

enum packet_type {
    PKT_DATA = 1, PKT_STATUS = 2, PKT_GAP = 3, PKT_DIAG = 4,
    CMD_READ = 0x80, CMD_HELLO = 0x81, CMD_BIND = 0x82
};

typedef struct {
    uint32_t session;
    uint32_t sequence;
    uint16_t count;
    uint16_t size;
    uint8_t type;
    uint8_t payload[PAYLOAD_CAPACITY];
} packet_t;

typedef struct {
    uint8_t bytes[FRAME_CAPACITY];
    uint16_t used;
    uint32_t errors;
} parser_t;

uint16_t read_u16(const uint8_t *p);
uint32_t read_u32(const uint8_t *p);
void write_u16(uint8_t *p, uint16_t value);
void write_u32(uint8_t *p, uint32_t value);
uint16_t crc16(const uint8_t *bytes, size_t size);
size_t packet_encode(const packet_t *packet, uint8_t *output);
int parser_push(parser_t *parser, uint8_t byte, packet_t *packet);

#endif

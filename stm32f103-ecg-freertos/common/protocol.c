#include "protocol.h"
#include <string.h>

uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t read_u32(const uint8_t *p)
{
    return read_u16(p) | ((uint32_t)read_u16(p + 2) << 16);
}

void write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

void write_u32(uint8_t *p, uint32_t value)
{
    write_u16(p, (uint16_t)value);
    write_u16(p + 2, (uint16_t)(value >> 16));
}

uint16_t crc16(const uint8_t *bytes, size_t size)
{
    uint16_t crc = 0xffff;
    while (size--) {
        crc ^= (uint16_t)*bytes++ << 8;
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
        }
    }
    return crc;
}

static int valid_shape(uint8_t type, uint16_t count, uint16_t size)
{
    switch (type) {
    case PKT_DATA: return count >= 1 && count <= PACKET_SAMPLES && size == count * 6;
    case PKT_STATUS: return count == 0 && size == 20;
    case PKT_GAP: return count == 0 && size == 8;
    case PKT_DIAG: return count == 0 && size == 48;
    case CMD_READ: return count >= 1 && count <= PACKET_SAMPLES && size == 0;
    case CMD_HELLO:
    case CMD_BIND: return count == 0 && size == 0;
    default: return 0;
    }
}

size_t packet_encode(const packet_t *packet, uint8_t *output)
{
    if (!valid_shape(packet->type, packet->count, packet->size)) {
        return 0;
    }
    output[0] = 0xa5;
    output[1] = 0x5a;
    output[2] = 1;
    output[3] = packet->type;
    write_u32(output + 4, packet->session);
    write_u32(output + 8, packet->sequence);
    write_u16(output + 12, packet->count);
    write_u16(output + 14, packet->size);
    memcpy(output + 16, packet->payload, packet->size);
    write_u16(output + 16 + packet->size, crc16(output + 2, 14 + packet->size));
    return 18 + packet->size;
}

static void discard_byte(parser_t *parser)
{
    parser->used--;
    memmove(parser->bytes, parser->bytes + 1, parser->used);
}

int parser_push(parser_t *parser, uint8_t byte, packet_t *packet)
{
    if (parser->used == FRAME_CAPACITY) {
        discard_byte(parser);
        parser->errors++;
    }
    parser->bytes[parser->used++] = byte;
    for (;;) {
        uint8_t *b = parser->bytes;
        if (!parser->used) return 0;
        if (b[0] != 0xa5 || (parser->used >= 2 && b[1] != 0x5a)) {
            discard_byte(parser);
            continue;
        }
        if (parser->used < 16) return 0;
        uint16_t count = read_u16(b + 12);
        uint16_t size = read_u16(b + 14);
        if (b[2] != 1 || !valid_shape(b[3], count, size)) {
            parser->errors++;
            discard_byte(parser);
            continue;
        }
        if (parser->used < size + 18) return 0;
        if (read_u16(b + 16 + size) != crc16(b + 2, 14 + size)) {
            parser->errors++;
            discard_byte(parser);
            continue;
        }
        packet->type = b[3];
        packet->session = read_u32(b + 4);
        packet->sequence = read_u32(b + 8);
        packet->count = count;
        packet->size = size;
        memcpy(packet->payload, b + 16, size);
        parser->used -= size + 18;
        memmove(b, b + size + 18, parser->used);
        return 1;
    }
}

#include "session.h"
#include <string.h>

static void new_binding(session_t *session)
{
    uint32_t id = GetTickCount() ^ (GetCurrentProcessId() << 16) ^ session->receiver.session;
    receiver_init(&session->receiver, id ? id : 1);
    memset(&session->parser, 0, sizeof(session->parser));
    session->bind_time = GetTickCount() - 1000;
}

int session_open(session_t *session, const char *port)
{
    memset(session, 0, sizeof(*session));
    session->serial = serial_open(port);
    if (session->serial == INVALID_HANDLE_VALUE) return 0;
    session->connected = 1;
    session->last_frame_time = GetTickCount();
    new_binding(session);
    return 1;
}

void session_close(session_t *session)
{
    if (session->connected) CloseHandle(session->serial);
    if (session->csv) fclose(session->csv);
    session->serial = INVALID_HANDLE_VALUE;
    session->connected = 0;
    session->csv = NULL;
}

int session_csv(session_t *session, const char *path)
{
    if (!session->connected) return 0;
    FILE *file = fopen(path, "w");
    if (!file) return 0;
    if (session->csv) fclose(session->csv);
    session->csv = file;
    fprintf(file, "session,seq,time_s,lead_I_code,lead_II_code,lead_III_code\n");
    return 1;
}

int session_stale(const session_t *session, uint32_t now)
{
    if (!session->connected) return 0;
    if (session->drop_length && now - session->drop_start < session->drop_length) return 0;
    return now - session->last_frame_time > 3000;
}

static void save_sample(void *context, uint32_t sequence, const uint16_t values[3])
{
    session_t *session = context;
    if (sequence > session->last_sequence) session->last_sequence = sequence;
    if (session->csv) {
        fprintf(session->csv, "%lu,%lu,%.3f,%u,%u,%u\n", (unsigned long)session->receiver.session,
                (unsigned long)sequence, sequence / 250.0, values[0], values[1], values[2]);
    }
}

static int send_command(session_t *session, const packet_t *packet)
{
    uint8_t bytes[FRAME_CAPACITY];
    unsigned size = (unsigned)packet_encode(packet, bytes);
    return size && serial_write(session->serial, bytes, size);
}

void session_drop(session_t *session, uint32_t now)
{
    session->drop_start = now;
    session->drop_length = 5000;
    session->parser.used = 0;
}

int session_step(session_t *session, uint32_t now)
{
    if (!session->connected) return 0;
    int dropping = session->drop_length && now - session->drop_start < session->drop_length;
    uint8_t bytes[4096];
    packet_t packet;
    /* Drain USB serial batches before choosing the next repair request. */
    for (unsigned batch = 0; batch < 64; batch++) {
        int count = serial_read(session->serial, bytes, sizeof(bytes));
        if (count < 0) return -1;
        if (!count) break;
        if (dropping) { session->dropped_bytes += (uint32_t)count; session->parser.used = 0; continue; }
        for (int i = 0; i < count; i++) {
            if (!parser_push(&session->parser, bytes[i], &packet)) continue;
            if (packet.type == PKT_STATUS && !packet.session) {
                if (session->receiver.started) new_binding(session);
                continue;
            }
            if (packet.session != session->receiver.session) continue;
            session->last_frame_time = now;
            receiver_accept(&session->receiver, &packet, save_sample, session);
        }
    }
    if (session->csv && ferror(session->csv)) return -2;
    if (dropping) return 1;
    if (!session->receiver.started && now - session->bind_time >= 500) {
        memset(&packet, 0, sizeof(packet));
        packet.type = CMD_BIND;
        packet.session = session->receiver.session;
        if (!send_command(session, &packet)) return -1;
        session->bind_time = now;
    }
    if (now - session->hello_time >= 1000) {
        memset(&packet, 0, sizeof(packet)); packet.type = CMD_HELLO;
        if (!send_command(session, &packet)) return -1;
        session->hello_time = now;
    }
    if (receiver_request(&session->receiver, now, &packet) && !send_command(session, &packet)) return -1;
    return 1;
}

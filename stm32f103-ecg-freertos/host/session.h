#ifndef ECG_SESSION_H
#define ECG_SESSION_H
#include "serial.h"
#include "receiver.h"
#include <stdio.h>
typedef struct {
    HANDLE serial;
    receiver_t receiver;
    parser_t parser;
    FILE *csv;
    uint32_t bind_time, hello_time, last_frame_time;
    uint32_t drop_start, drop_length, dropped_bytes, last_sequence;
    int connected;
} session_t;
int session_open(session_t *session, const char *port);
void session_close(session_t *session);
int session_step(session_t *session, uint32_t now);
void session_drop(session_t *session, uint32_t now);
int session_csv(session_t *session, const char *path);
int session_stale(const session_t *session, uint32_t now);
#endif

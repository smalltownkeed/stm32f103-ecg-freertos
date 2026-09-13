#include "session.h"
#include <stdio.h>
#include <stdlib.h>
static session_t session;

int main(int argc, char **argv)
{
    const char *port = argc > 1 ? argv[1] : "COM7";
    if (!session_open(&session, port)) { fprintf(stderr, "Cannot open %s, Windows error %lu\n", port, GetLastError()); return 1; }
    uint32_t start = GetTickCount(), first = 0;
    int injected = 0, was_started = 0;
    while (GetTickCount() - start < 16000) {
        uint32_t now = GetTickCount();
        if (session_step(&session, now) < 0) { fprintf(stderr,"Serial or CSV I/O failed\n"); session_close(&session); return 1; }
        if (session.receiver.started && !was_started) { first = session.receiver.floor; was_started = 1; }
        if (was_started && !injected && now - start >= 2000) { session_drop(&session, now); injected = 1; }
        Sleep(2);
    }
    receiver_t *r = &session.receiver;
    uint32_t expected = was_started ? session.last_sequence - first + 1 : 0;
    printf("{\"bound\":%u,\"received\":%lu,\"expected_through_last\":%lu,\"expired\":%lu,\"missing\":%lu,\"dropped_bytes\":%lu,\"adc_faults\":%lu,\"rx_errors\":%lu,\"parser_errors\":%lu,\"diagnostics\":[",
           r->started,(unsigned long)r->received,(unsigned long)expected,(unsigned long)r->expired,
           (unsigned long)receiver_missing(r),(unsigned long)session.dropped_bytes,
           (unsigned long)r->adc_faults,(unsigned long)r->rx_errors,(unsigned long)session.parser.errors);
    for (unsigned i = 0; i < 12; i++) printf("%s%lu", i ? "," : "", (unsigned long)r->diagnostics[i]);
    puts("]}");
    int passed = was_started && injected && session.dropped_bytes && r->received == expected &&
                 !r->expired && !r->adc_faults && !r->rx_errors && !session.parser.errors &&
                 r->diagnostics[9] == 4 && r->diagnostics[10] == 2 && r->diagnostics[11] == 1;
    session_close(&session);
    return passed ? 0 : 2;
}

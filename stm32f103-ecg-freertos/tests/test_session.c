#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL session line %d\n",__LINE__); exit(1); } } while(0)
static session_t session;
int main(void)
{
    CHECK(!session_csv(&session,"must_not_be_created.csv"));
    CHECK(session.csv == NULL);
    CHECK(!session_stale(&session,5000));
    session.connected = 1;
    session.last_frame_time = 1000;
    CHECK(!session_stale(&session,4000));
    CHECK(session_stale(&session,4001));
    session.drop_start = 4000;
    session.drop_length = 5000;
    CHECK(!session_stale(&session,8500));
    CHECK(session_stale(&session,9001));
    session.last_frame_time = 9100;
    CHECK(!session_stale(&session,9200));
    puts("PASS host: refuse disconnected CSV, stalled-stream warning, intentional-drop suppression");
    return 0;
}

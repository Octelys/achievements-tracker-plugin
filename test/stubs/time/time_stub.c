#include "test/stubs/time/time_stub.h"

static time_t mock_current_time;

void mock_now(time_t current_time) {
    mock_current_time = current_time;
}

time_t now() {
    (void)time;
    return mock_current_time;
}

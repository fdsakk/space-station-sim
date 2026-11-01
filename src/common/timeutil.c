/* Pomocnicze funkcje czasu. */
#include "timeutil.h"

#include "die.h"

struct timespec time_deadline(int timeout_ms)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        die("clock_gettime");
    }
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return ts;
}

long long time_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        die("clock_gettime");
    }
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000L;
}

long long time_wall_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        die("clock_gettime");
    }
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000L;
}

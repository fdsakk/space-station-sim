/* Testy bufora cyklicznego dziennika (log_ring_push). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/common/log.h"

static void test_push_appends_in_order(void)
{
    struct log_ring ring = {0};
    log_ring_push(&ring, "pierwszy");
    log_ring_push(&ring, "drugi");
    assert(ring.count == 2);
    assert(ring.next == 2);
    assert(strcmp(ring.lines[0], "pierwszy") == 0);
    assert(strcmp(ring.lines[1], "drugi") == 0);
}

static void test_push_overwrites_oldest_when_full(void)
{
    struct log_ring ring = {0};
    for (int i = 0; i <= LOG_LINES; i++) {
        char line[16];
        snprintf(line, sizeof line, "wpis %d", i);
        log_ring_push(&ring, line);
    }
    assert(ring.count == LOG_LINES);
    assert(ring.next == 1);
    /* Wpis numer LOG_LINES nadpisał najstarszy (wpis 0). */
    char expected[16];
    snprintf(expected, sizeof expected, "wpis %d", LOG_LINES);
    assert(strcmp(ring.lines[0], expected) == 0);
}

static void test_push_truncates_long_line(void)
{
    struct log_ring ring = {0};
    char line[LOG_LINE_LEN * 2];
    memset(line, 'x', sizeof line - 1);
    line[sizeof line - 1] = '\0';
    log_ring_push(&ring, line);
    assert(strlen(ring.lines[0]) == LOG_LINE_LEN - 1);
}

int main(void)
{
    test_push_appends_in_order();
    test_push_overwrites_oldest_when_full();
    test_push_truncates_long_line();
    puts("test_log_ring: OK");
    return 0;
}

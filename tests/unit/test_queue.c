/* Testy kolejki zgłoszeń: priorytet, ochrona przed zagłodzeniem, powrót na początek. */
#include <assert.h>
#include <stdio.h>

#include "../../src/station/queue.h"

static struct queue_entry entry(pid_t pid, enum ship_type type, long long since_ms)
{
    struct queue_entry e = {pid, type, since_ms};
    return e;
}

static void test_same_priority_in_arrival_order(void)
{
    struct ship_queue q = {0};
    queue_push(&q, entry(1, SHIP_TYPE_SHUTTLE, 0));
    queue_push(&q, entry(2, SHIP_TYPE_FREIGHTER, 0));
    assert(queue_pop(&q, 0).pid == 1);
    assert(queue_pop(&q, 0).pid == 2);
    assert(q.count == 0);
}

static void test_emergency_goes_first(void)
{
    struct ship_queue q = {0};
    queue_push(&q, entry(1, SHIP_TYPE_SHUTTLE, 0));
    queue_push(&q, entry(2, SHIP_TYPE_EMERGENCY, 0));
    queue_push(&q, entry(3, SHIP_TYPE_EMERGENCY, 0));
    assert(queue_pop(&q, 0).pid == 2);
    assert(queue_pop(&q, 0).pid == 3);
    assert(queue_pop(&q, 0).pid == 1);
}

static void test_starving_ship_treated_as_emergency(void)
{
    struct ship_queue q = {0};
    queue_push(&q, entry(1, SHIP_TYPE_SHUTTLE, 0));
    queue_push(&q, entry(2, SHIP_TYPE_EMERGENCY, 1000));
    /* Chwilę przed progiem awaryjny jest pierwszy... */
    struct ship_queue before = q;
    assert(queue_pop(&before, STARVATION_MS - 1).pid == 2);
    /* ...a po progu zwykły statek jest pilny i zgłosił się wcześniej. */
    assert(queue_pop(&q, STARVATION_MS).pid == 1);
}

static void test_push_front_within_priority_group(void)
{
    struct ship_queue q = {0};
    queue_push(&q, entry(1, SHIP_TYPE_EMERGENCY, 0));
    queue_push(&q, entry(2, SHIP_TYPE_SHUTTLE, 0));
    queue_push_front(&q, entry(3, SHIP_TYPE_FREIGHTER, 0));
    assert(queue_pop(&q, 0).pid == 1); /* pilny nadal pierwszy */
    assert(queue_pop(&q, 0).pid == 3); /* powracający przed resztą zwykłych */
    assert(queue_pop(&q, 0).pid == 2);
}

static void test_remove(void)
{
    struct ship_queue q = {0};
    queue_push(&q, entry(1, SHIP_TYPE_SHUTTLE, 0));
    queue_push(&q, entry(2, SHIP_TYPE_SHUTTLE, 0));
    queue_push(&q, entry(3, SHIP_TYPE_SHUTTLE, 0));
    queue_remove(&q, 2);
    queue_remove(&q, 99); /* brak w kolejce: bez zmian */
    assert(q.count == 2);
    assert(queue_pop(&q, 0).pid == 1);
    assert(queue_pop(&q, 0).pid == 3);
}

int main(void)
{
    test_same_priority_in_arrival_order();
    test_emergency_goes_first();
    test_starving_ship_treated_as_emergency();
    test_push_front_within_priority_group();
    test_remove();
    puts("test_queue: OK");
    return 0;
}

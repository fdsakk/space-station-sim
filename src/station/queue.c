/* Kolejka zgłoszeń statków: kolejność obsługi (priorytet, ochrona przed zagłodzeniem).
 * Elementy leżą w kolejności przybycia; queue_pop wybiera pierwszy pilny, a gdy go nie ma,
 * pierwszy w ogóle. Dzięki temu w każdej grupie priorytetu obowiązuje kolejność zgłoszeń. */
#include "queue.h"

/* Pilny = awaryjny albo czeka już tak długo, że traktujemy go jak awaryjny. */
static int is_urgent(const struct queue_entry *e, long long now_ms)
{
    return e->type == SHIP_TYPE_EMERGENCY || now_ms - e->since_ms >= STARVATION_MS;
}

static void remove_at(struct ship_queue *q, int index)
{
    for (int i = index; i < q->count - 1; i++) {
        q->items[i] = q->items[i + 1];
    }
    q->count--;
}

void queue_push(struct ship_queue *q, struct queue_entry e)
{
    q->items[q->count] = e;
    q->count++;
}

void queue_push_front(struct ship_queue *q, struct queue_entry e)
{
    for (int i = q->count; i > 0; i--) {
        q->items[i] = q->items[i - 1];
    }
    q->items[0] = e;
    q->count++;
}

struct queue_entry queue_pop(struct ship_queue *q, long long now_ms)
{
    int chosen = 0;
    for (int i = 0; i < q->count; i++) {
        if (is_urgent(&q->items[i], now_ms)) {
            chosen = i;
            break;
        }
    }
    struct queue_entry e = q->items[chosen];
    remove_at(q, chosen);
    return e;
}

void queue_remove(struct ship_queue *q, pid_t pid)
{
    for (int i = 0; i < q->count; i++) {
        if (q->items[i].pid == pid) {
            remove_at(q, i);
            return;
        }
    }
}

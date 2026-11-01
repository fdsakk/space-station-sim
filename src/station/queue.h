/* Kolejka zgłoszeń statków: kolejność obsługi (priorytet, ochrona przed zagłodzeniem).
 * Czyste funkcje bez IPC - kolejkę trzyma sterownik. */
#ifndef QUEUE_H
#define QUEUE_H

#include "../common/state.h"

/* Dopisuje statek na koniec. Wywołujący pilnuje, żeby kolejka nie była pełna. */
void queue_push(struct ship_queue *q, struct queue_entry e);
/* Wstawia statek na początek (powrót po awarii portu). */
void queue_push_front(struct ship_queue *q, struct queue_entry e);
/* Zdejmuje statek, który ma być obsłużony jako następny. Kolejka nie może być pusta. */
struct queue_entry queue_pop(struct ship_queue *q, long long now_ms);
/* Usuwa statek o danym pid, jeśli jest w kolejce. */
void queue_remove(struct ship_queue *q, pid_t pid);

#endif

/* Dziennik zdarzeń: każdy wpis trafia do pliku stacja.log i do bufora w pamięci dzielonej. */
#ifndef LOG_H
#define LOG_H

#include <semaphore.h>

#include "state.h"

/* who - nazwa procesu w dzienniku, np. "port 1". */
void log_init(const char *who, struct station_state *state, sem_t *mutex);
void log_close(void);
void log_event(const char *fmt, ...);

/* Czysta funkcja bufora cyklicznego (bez blokady) - wywołujący musi trzymać mutex. */
void log_ring_push(struct log_ring *ring, const char *line);

#endif

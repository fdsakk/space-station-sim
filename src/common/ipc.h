/* Tworzenie, otwieranie i usuwanie obiektów IPC stacji oraz blokada pamięci dzielonej.
 * Nazwy funkcji mają prefiks ipc_, bo prefiksy shm_, sem_ i mq_ są zarezerwowane przez POSIX. */
#ifndef IPC_H
#define IPC_H

#include <mqueue.h>
#include <semaphore.h>
#include <stddef.h>

#include "protocol.h"
#include "state.h"

/* Pamięć dzielona: tworzy tylko station, pozostałe procesy się dołączają. */
struct station_state *ipc_shm_create(void);
struct station_state *ipc_shm_attach(void);
void ipc_shm_detach(struct station_state *state);

/* Semafory nazwane. */
sem_t *ipc_sem_create(const char *name, unsigned int value);
sem_t *ipc_sem_attach(const char *name);
void ipc_sem_close(sem_t *sem);

/* Wyłączny dostęp do pamięci dzielonej (semafor /ss_mutex). */
void ipc_lock(sem_t *mutex);
void ipc_unlock(sem_t *mutex);

/* Kolejki komunikatów przenoszące struct msg. */
/* flags: 0 albo O_NONBLOCK (twórca tylko wysyła i nie może czekać na odbiorcę). */
mqd_t ipc_mq_create(const char *name, int flags);
mqd_t ipc_mq_attach(const char *name, int flags);
/* Zwraca 1, gdy odebrano komunikat; 0, gdy minął czas albo przerwał sygnał. */
int ipc_mq_receive(mqd_t queue, struct msg *out, int timeout_ms);
/* Wysyła do kolejki blokującej. Gdy przerwie sygnał zatrzymania, komunikat przepada
 * (proces i tak kończy pracę). */
void ipc_mq_send(mqd_t queue, const struct msg *m);
/* Wysyła do kolejki otwartej z O_NONBLOCK. Zwraca 0, gdy kolejka jest pełna
 * (odbiorca nie odbiera). */
int ipc_mq_try_send(mqd_t queue, const struct msg *m);
void ipc_mq_close(mqd_t queue);
void ipc_port_queue_name(int port, char *buf, size_t len);
void ipc_ship_queue_name(pid_t ship, char *buf, size_t len);

/* Usuwa wszystkie nazwane obiekty stacji (przy zatrzymaniu; brak obiektu nie jest błędem). */
void ipc_unlink_all(void);

#endif

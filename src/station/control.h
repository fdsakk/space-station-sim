/* Logika sterownika: zgłoszenia statków, przydział portów, pośredniczenie statek <-> port,
 * limit czasu potwierdzenia przydziału, utrata statku, awarie portów, komendy operatora. */
#ifndef CONTROL_H
#define CONTROL_H

#include <mqueue.h>
#include <semaphore.h>

#include "../common/config.h"
#include "../common/protocol.h"
#include "../common/state.h"

void control_init(struct station_state *state, sem_t *mutex, sem_t *energy,
                  const mqd_t ports[PORT_COUNT], mqd_t gen);
/* Obsługa jednego komunikatu z /ss_ctrl. Zwraca 0, gdy operator zatrzymał stację. */
int control_handle(const struct msg *m);
/* Co cykl pętli: procesy portów, limity czasu potwierdzeń, przydział wolnych portów,
 * publikacja stanu. */
void control_tick(void);
/* Zamyka kolejki statków, które jeszcze są w systemie (przy zatrzymaniu stacji). */
void control_close(void);

#endif

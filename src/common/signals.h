/* Obsługa SIGINT, SIGTERM i SIGHUP: funkcja obsługi tylko ustawia flagę,
 * a pętla główna ją sprawdza. */
#ifndef SIGNALS_H
#define SIGNALS_H

#include <sys/types.h>

void signals_setup(void);
/* fork, po którym dziecko nie może zgubić sygnału zatrzymania przed swoim exec. */
pid_t signals_fork(void);
int signals_stop_requested(void);

#endif

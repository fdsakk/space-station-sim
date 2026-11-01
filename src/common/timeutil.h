/* Pomocnicze funkcje czasu. */
#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include <time.h>

/* Chwila "teraz + timeout_ms" na zegarze CLOCK_REALTIME - takiej (bezwzględnej)
 * wartości wymaga mq_timedreceive. */
struct timespec time_deadline(int timeout_ms);
/* Bieżący czas w ms na zegarze CLOCK_MONOTONIC. Do odmierzania czasu etapów: ten zegar
 * nie cofa się przy zmianie zegara systemowego. */
long long time_now_ms(void);
/* Bieżący czas w ms od 1970 r. (CLOCK_REALTIME). Do znaczników pakietów sieciowych: operator
 * i brama działają na różnych komputerach, więc potrzebują wspólnego zegara ściennego. */
long long time_wall_ms(void);

#endif

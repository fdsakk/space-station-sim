/* Nadzór nad procesami potomnymi stacji: uruchamianie (fork + exec), zbieranie, zatrzymanie. */
#ifndef SUPERVISOR_H
#define SUPERVISOR_H

/* Uruchamia BIN_DIR/program z jednym argumentem (NULL = bez argumentów). */
void supervisor_spawn(const char *program, const char *arg);
/* Uruchamia proces portu o danym indeksie (także ponownie, po śmierci poprzedniego). */
void supervisor_spawn_port(int port);
/* 1, gdy proces portu działa (nie został jeszcze zebrany przez supervisor_reap). */
int supervisor_port_running(int port);
/* Zbiera procesy, które się zakończyły, i zapisuje to w dzienniku. Nie blokuje. */
void supervisor_reap(void);
/* Wysyła SIGTERM do wszystkich procesów potomnych i czeka na ich zakończenie. */
void supervisor_stop_all(void);

#endif

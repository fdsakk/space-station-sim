/* Komendy operatora: wiersz tekstu (z panelu albo z sieci) -> komunikat OP_* dla
 * sterownika. Jeden parser dla obu dróg, żeby rozumiały komendy dokładnie tak samo. */
#ifndef COMMANDS_H
#define COMMANDS_H

#include "protocol.h"

/* Podpowiedź dla operatora. */
#define COMMANDS_HELP "awaria <1-3>, napraw <1-3>, statek <typ>, fala, shutdown"

/* Zamienia wiersz komendy na komunikat OP_* (pole sender uzupełnia wysyłający).
 * Zwraca 0, gdy komenda jest błędna. */
int commands_parse(const char *line, struct msg *out);

#endif

/* Zakończenie procesu po błędzie, którego nie da się obsłużyć (np. przy starcie). */
#include "die.h"

#include <stdio.h>
#include <stdlib.h>

_Noreturn void die(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

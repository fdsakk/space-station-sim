/* Zakończenie procesu po błędzie, którego nie da się obsłużyć (np. przy starcie). */
#ifndef DIE_H
#define DIE_H

/* _Noreturn (C11): kompilator i cppcheck wiedzą, że po die kod dalej nie biegnie. */
_Noreturn void die(const char *what);

#endif

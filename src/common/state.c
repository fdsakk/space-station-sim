/* Nazwy stanów z pamięci dzielonej, wspólne dla sterownika (dziennik) i panelu (ekran). */
#include "state.h"

const char *ship_state_name(enum ship_state state)
{
    switch (state) {
    case SHIP_WAITING:
        return "OCZEKUJE";
    case SHIP_ASSIGNED:
        return "PRZYDZIELONY";
    case SHIP_DOCKING:
        return "DOKUJE";
    case SHIP_DOCKED:
        return "ZADOKOWANY";
    case SHIP_UNDOCKING:
        return "ODDOKOWUJE";
    case SHIP_DEPARTED:
        return "ODLECIAL";
    case SHIP_LOST:
        return "UTRACONY";
    }
    return "NIEZNANY";
}

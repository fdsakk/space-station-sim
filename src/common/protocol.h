/* Protokół komunikatów między procesami (kolejki POSIX).
 * Pełna lista typów jest umową między modułami - zmiana typu dotyka wielu procesów. */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/types.h>

enum msg_type {
    /* statek -> sterownik */
    SHIP_HELLO,
    SHIP_ACK_ASSIGN,
    SHIP_UNDOCK_REQ,
    /* sterownik -> statek */
    ASSIGNED,
    DOCKED,
    SERVICE_DONE,
    DEPART_OK,
    PORT_FAULT,
    /* sterownik -> port */
    CMD_DOCK,
    CMD_UNDOCK,
    CMD_FAULT,
    CMD_REPAIR,
    CMD_CANCEL,
    /* port -> sterownik */
    EV_DOCKED,
    EV_SERVICE_DONE,
    EV_UNDOCKED,
    EV_FAULT,
    /* sterownik <-> generator */
    CMD_SPAWN,
    CMD_WAVE,
    EV_SHIP_LOST,
    /* panel / brama -> sterownik */
    OP_FAULT,
    OP_REPAIR,
    OP_SPAWN,
    OP_WAVE,
    OP_SHUTDOWN
};

enum ship_type { SHIP_TYPE_SHUTTLE, SHIP_TYPE_FREIGHTER, SHIP_TYPE_EMERGENCY };
#define SHIP_TYPE_COUNT 3 /* liczba wartości enum ship_type */

struct msg {
    int type;      /* enum msg_type */
    pid_t sender;  /* pid nadawcy */
    pid_t ship;    /* pid statku, którego dotyczy komunikat (0 = żaden) */
    int port;      /* numer portu, gdy dotyczy */
    int ship_type; /* enum ship_type, gdy dotyczy */
    int arg;       /* dodatkowa wartość, np. numer sygnału */
};

const char *msg_type_name(enum msg_type type);
/* Nazwa typu statku: w dzienniku i jako argument programu ship (np. "awaryjny"). */
const char *ship_type_name(enum ship_type type);
/* Zamienia nazwę typu na enum; zwraca 0, gdy nazwa jest nieznana. */
int ship_type_parse(const char *name, enum ship_type *out);

#endif

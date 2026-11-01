/* Układ pamięci dzielonej /ss_state - stan stacji widoczny dla wszystkich procesów stacji.
 * Każde pole ma jednego pisarza; kto nim jest, opisują komentarze przy strukturach. */
#ifndef STATE_H
#define STATE_H

#include <sys/types.h>

#include "config.h"
#include "protocol.h"

/* Bufor cykliczny ostatnich zdarzeń - panel pokazuje go na ekranie. */
struct log_ring {
    int next;  /* indeks, pod który trafi następny wpis */
    int count; /* liczba zapisanych wpisów, najwyżej LOG_LINES */
    char lines[LOG_LINES][LOG_LINE_LEN];
};

/* Pierwsze wartości enumów (WOLNY, ZAMKNIETA, BRAK) to stan po utworzeniu pamięci,
 * bo ftruncate wypełnia ją zerami. */
enum port_state {
    PORT_FREE,
    PORT_RESERVED,
    PORT_DOCKING,
    PORT_DOCKED,
    PORT_UNDOCKING,
    PORT_BROKEN,
    PORT_REPAIRING
};

enum lock_state { LOCK_CLOSED, LOCK_EQUALIZING, LOCK_OPEN, LOCK_CLOSING };

enum service_state { SERVICE_NONE, SERVICE_WAITING_ENERGY, SERVICE_RUNNING, SERVICE_FINISHED };

/* Rekord portu. Rezerwację (state, ship, ship_type) zapisuje sterownik, resztę sam port. */
struct port_record {
    enum port_state state;
    enum lock_state lock;
    enum service_state service;
    pid_t ship; /* statek przy porcie (od rezerwacji); 0 = brak */
    enum ship_type ship_type;
    int has_energy;         /* 1 = port trzyma jednostkę energii */
    long long timer_end_ms; /* koniec bieżącego etapu (time_now_ms) */
};

enum ship_state {
    SHIP_WAITING,
    SHIP_ASSIGNED,
    SHIP_DOCKING,
    SHIP_DOCKED,
    SHIP_UNDOCKING,
    SHIP_DEPARTED,
    SHIP_LOST
};

/* Rekord statku prowadzony przez sterownik. */
struct ship_record {
    pid_t pid; /* 0 = wolne miejsce */
    enum ship_type type;
    enum ship_state state;
    int port; /* przydzielony port; -1 = brak */
};

struct queue_entry {
    pid_t pid;
    enum ship_type type;
    long long since_ms; /* chwila zgłoszenia (time_now_ms) */
};

/* Kolejka zgłoszeń w kolejności przybycia; kolejność obsługi wybiera queue_pop. */
struct ship_queue {
    int count;
    struct queue_entry items[SHIP_MAX];
};

struct station_state {
    struct port_record ports[PORT_COUNT];
    struct ship_record ships[SHIP_MAX];
    struct ship_queue queue;
    struct log_ring log;
    int stopping; /* 1 = stacja kończy pracę (pisze station; panel zamyka się razem z nią) */
};

const char *ship_state_name(enum ship_state state);

#endif

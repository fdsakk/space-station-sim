/* Automat portu dokującego razem ze śluzą i obsługą.
 * Czyste funkcje bez IPC: proces portu woła je pod mutexem i sam wykonuje zwrócone akcje. */
#ifndef PORT_FSM_H
#define PORT_FSM_H

#include "../common/protocol.h"
#include "../common/state.h"

/* Co proces portu ma zrobić po kroku automatu. */
struct port_action {
    int send;            /* 1 = wyślij zdarzenie event do sterownika (po zwolnieniu mutexu) */
    enum msg_type event; /* EV_* */
    pid_t ship;          /* statek, którego dotyczy zdarzenie albo odrzucone polecenie */
    int rejected;        /* 1 = polecenie odrzucone (port zapisuje to w dzienniku) */
    enum msg_type rejected_type; /* typ odrzuconego polecenia */
    int release_energy;          /* 1 = oddaj jednostkę energii (sem_post, jeszcze pod mutexem) */
};

/* Rezerwacja przez sterownik: WOLNY -> ZAREZERWOWANY. Sterownik sprawdza wcześniej pod tym
 * samym mutexem, że port jest wolny. */
void port_fsm_reserve(struct port_record *p, pid_t ship, enum ship_type type);
/* Polecenie od sterownika (CMD_*). Polecenie niewykonalne w danym stanie jest odrzucane. */
struct port_action port_fsm_command(struct port_record *p, const struct msg *cmd, long long now_ms);
/* Upływ czasu: kończy etap, którego czas minął. Wołane w każdym cyklu pętli portu. */
struct port_action port_fsm_tick(struct port_record *p, long long now_ms);
/* 1, gdy port czeka na jednostkę energii (obsługa albo naprawa). */
int port_fsm_needs_energy(const struct port_record *p);
/* Port dostał jednostkę energii (udany sem_trywait): rusza obsługa albo naprawa. */
void port_fsm_energy_taken(struct port_record *p, long long now_ms);

const char *port_state_name(enum port_state state);
const char *lock_state_name(enum lock_state state);
const char *service_state_name(enum service_state state);

#endif

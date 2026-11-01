/* Proces statku: zgłasza się do sterownika, potwierdza przydział, czeka na obsługę i odlatuje.
 * Rozmawia ze stacją tylko przez kolejki komunikatów, bez pamięci dzielonej. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/config.h"
#include "../common/ipc.h"
#include "../common/signals.h"

/* Automat statku. */
enum ship_phase { PHASE_REPORTED, PHASE_TO_PORT, PHASE_DOCKED, PHASE_UNDOCKING, PHASE_DEPARTED };

struct ship {
    pid_t pid;
    enum ship_type type;
    int no_ack; /* 1 = nie potwierdza przydziału (scenariusz S6) */
    mqd_t ctrl;
    enum ship_phase phase;
};

static void usage(void)
{
    fprintf(stderr, "użycie: ship <typ> [--no-ack], typ:");
    for (int i = 0; i < SHIP_TYPE_COUNT; i++) {
        fprintf(stderr, " %s", ship_type_name((enum ship_type)i));
    }
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static void parse_args(int argc, char *argv[], struct ship *s)
{
    int ok = argc == 2 || (argc == 3 && strcmp(argv[2], "--no-ack") == 0);
    if (!ok || !ship_type_parse(argv[1], &s->type)) {
        usage();
    }
    s->no_ack = argc == 3;
}

static void send_to_station(const struct ship *s, enum msg_type type)
{
    struct msg m = {0};
    m.type = type;
    m.sender = s->pid;
    m.ship = s->pid;
    m.ship_type = s->type;
    ipc_mq_send(s->ctrl, &m);
}

static void handle(struct ship *s, const struct msg *m)
{
    switch (m->type) {
    case ASSIGNED:
        /* Statek z --no-ack udaje zerwaną łączność: milczy, a sterownik po ACK_TIMEOUT_MS
         * anuluje rezerwację i kończy ten proces. */
        if (!s->no_ack) {
            send_to_station(s, SHIP_ACK_ASSIGN);
            s->phase = PHASE_TO_PORT;
        }
        break;
    case DOCKED:
        s->phase = PHASE_DOCKED;
        break;
    case SERVICE_DONE:
        send_to_station(s, SHIP_UNDOCK_REQ);
        s->phase = PHASE_UNDOCKING;
        break;
    case DEPART_OK:
        s->phase = PHASE_DEPARTED;
        break;
    case PORT_FAULT:
        /* Port się zepsuł: statek jest z powrotem w kolejce i czeka na nowy przydział. */
        s->phase = PHASE_REPORTED;
        break;
    default:
        /* Pozostałe typy komunikatów nie są adresowane do statku. */
        break;
    }
}

int main(int argc, char *argv[])
{
    struct ship s = {0};
    parse_args(argc, argv, &s);
    s.pid = getpid();
    signals_setup();

    /* Najpierw kolejka sterownika: gdy stacja nie działa, kończymy, zanim utworzymy własną. */
    s.ctrl = ipc_mq_attach(NAME_CTRL, O_WRONLY);
    char name[NAME_LEN];
    ipc_ship_queue_name(s.pid, name, sizeof name);
    mqd_t inbox = ipc_mq_create(name, 0);
    send_to_station(&s, SHIP_HELLO);

    while (s.phase != PHASE_DEPARTED && !signals_stop_requested()) {
        struct msg m;
        if (ipc_mq_receive(inbox, &m, LOOP_TICK_MS)) {
            handle(&s, &m);
        }
    }

    ipc_mq_close(inbox);
    ipc_mq_close(s.ctrl);
    /* Kolejkę statku tworzy i usuwa sam statek (wyjątek od zasady, że IPC tworzy stacja). */
    if (mq_unlink(name) == -1) {
        perror(name);
    }
    return EXIT_SUCCESS;
}

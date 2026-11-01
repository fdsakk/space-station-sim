/* Proces portu dokującego: wykonuje automat portu (port_fsm) na swoim rekordzie w pamięci
 * dzielonej, pobiera i oddaje energię, zgłasza zdarzenia sterownikowi. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../common/config.h"
#include "../common/die.h"
#include "../common/ipc.h"
#include "../common/log.h"
#include "../common/signals.h"
#include "../common/timeutil.h"
#include "port_fsm.h"

/* Argument to numer portu dla ludzi (1..PORT_COUNT); zwraca indeks w tablicy ports[]. */
static int parse_port(const char *text)
{
    char *end;
    long number = text != NULL ? strtol(text, &end, 10) : -1;
    /* Pusty napis: strtol nic nie przeczytał, a *end == '\0', więc trzeba go odrzucić osobno. */
    if (text == NULL || *text == '\0' || *end != '\0' || number < 1 || number > PORT_COUNT) {
        fprintf(stderr, "użycie: port <numer 1..%d>\n", PORT_COUNT);
        exit(EXIT_FAILURE);
    }
    return (int)number - 1;
}

/* sem_trywait nie blokuje, więc wolno go wołać z mutexem. */
static int take_energy(sem_t *energy)
{
    if (sem_trywait(energy) == 0) {
        return 1;
    }
    if (errno != EAGAIN && errno != EINTR) {
        die("sem_trywait");
    }
    return 0;
}

static void release_energy(sem_t *energy, struct port_action a)
{
    if (a.release_energy && sem_post(energy) == -1) {
        die("sem_post");
    }
}

/* Jeden cykl automatu; wołany pod mutexem. Zwraca liczbę akcji zapisanych w out. */
static int step(struct port_record *p, sem_t *energy, const struct msg *cmd,
                struct port_action out[2])
{
    long long now = time_now_ms();
    int n = 0;
    if (cmd != NULL) {
        out[n] = port_fsm_command(p, cmd, now);
        release_energy(energy, out[n++]);
    }
    out[n] = port_fsm_tick(p, now);
    release_energy(energy, out[n++]);
    if (port_fsm_needs_energy(p) && take_energy(energy)) {
        port_fsm_energy_taken(p, now);
    }
    return n;
}

static void log_changes(const struct port_record *old, const struct port_record *now)
{
    if (old->state != now->state) {
        log_event("port: %s -> %s", port_state_name(old->state), port_state_name(now->state));
    }
    if (old->lock != now->lock) {
        log_event("śluza: %s -> %s", lock_state_name(old->lock), lock_state_name(now->lock));
    }
    if (old->service != now->service) {
        log_event("obsługa: %s -> %s", service_state_name(old->service),
                  service_state_name(now->service));
    }
}

static void report(mqd_t ctrl, int port, struct port_action a)
{
    if (a.rejected) {
        log_event("odrzucono %s (statek %d)", msg_type_name(a.rejected_type), (int)a.ship);
    }
    if (!a.send) {
        return;
    }
    struct msg m = {0};
    m.type = a.event;
    m.sender = getpid();
    m.ship = a.ship;
    m.port = port;
    ipc_mq_send(ctrl, &m);
}

int main(int argc, char *argv[])
{
    int port = parse_port(argc == 2 ? argv[1] : NULL);
    signals_setup();

    struct station_state *state = ipc_shm_attach();
    sem_t *mutex = ipc_sem_attach(NAME_MUTEX);
    sem_t *energy = ipc_sem_attach(NAME_ENERGY);
    char who[NAME_LEN];
    snprintf(who, sizeof who, "port %d", port + 1);
    log_init(who, state, mutex);

    char name[NAME_LEN];
    ipc_port_queue_name(port, name, sizeof name);
    mqd_t inbox = ipc_mq_attach(name, O_RDONLY);
    mqd_t ctrl = ipc_mq_attach(NAME_CTRL, O_WRONLY);
    log_event("start");

    while (!signals_stop_requested()) {
        struct msg m;
        int received = ipc_mq_receive(inbox, &m, LOOP_TICK_MS);

        /* Sekcja krytyczna: krok automatu na rekordzie i energia. Dziennik i wysyłanie potem. */
        struct port_action actions[2];
        ipc_lock(mutex);
        struct port_record *p = &state->ports[port];
        struct port_record before = *p;
        int n = step(p, energy, received ? &m : NULL, actions);
        struct port_record after = *p;
        ipc_unlock(mutex);

        log_changes(&before, &after);
        for (int i = 0; i < n; i++) {
            report(ctrl, port, actions[i]);
        }
    }

    log_event("koniec");
    log_close();
    ipc_mq_close(ctrl);
    ipc_mq_close(inbox);
    ipc_sem_close(energy);
    ipc_sem_close(mutex);
    ipc_shm_detach(state);
    return EXIT_SUCCESS;
}

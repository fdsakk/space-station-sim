/* Generator ruchu: uruchamia procesy statków (fork + exec) w losowych odstępach i na polecenie
 * sterownika, zbiera je (waitpid) i zgłasza sterownikowi statki zakończone nienormalnie. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../common/config.h"
#include "../common/die.h"
#include "../common/ipc.h"
#include "../common/log.h"
#include "../common/signals.h"
#include "../common/timeutil.h"

/* Pid-y działających statków; 0 = wolne miejsce. Ich liczba to limit SHIP_MAX. */
static pid_t ships[SHIP_MAX];

static int find_slot(pid_t pid)
{
    for (int i = 0; i < SHIP_MAX; i++) {
        if (ships[i] == pid) {
            return i;
        }
    }
    return -1;
}

static void spawn_ship(enum ship_type type)
{
    int slot = find_slot(0);
    if (slot == -1) {
        log_event("limit %d statków - nie uruchomiono statku (%s)", SHIP_MAX, ship_type_name(type));
        return;
    }
    char path[PATH_LEN];
    snprintf(path, sizeof path, "%s/ship", BIN_DIR);

    pid_t pid = signals_fork();
    if (pid == -1) {
        /* Brak zasobów w trakcie pracy: pomijamy ten statek, stacja działa dalej. */
        log_event("fork: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        execl(path, "ship", ship_type_name(type), (char *)NULL);
        perror(path);
        _exit(EXIT_FAILURE);
    }
    ships[slot] = pid;
    log_event("uruchomiono statek %d (%s)", (int)pid, ship_type_name(type));
}

static enum ship_type random_type(void)
{
    return (enum ship_type)(rand() % SHIP_TYPE_COUNT);
}

static long long next_spawn_time(long long now_ms)
{
    return now_ms + SPAWN_MIN_MS + rand() % (SPAWN_MAX_MS - SPAWN_MIN_MS + 1);
}

static void handle_command(const struct msg *m)
{
    switch (m->type) {
    case CMD_SPAWN:
        spawn_ship((enum ship_type)m->ship_type);
        break;
    case CMD_WAVE:
        for (int i = 0; i < WAVE_SIZE; i++) {
            spawn_ship(random_type());
        }
        break;
    default:
        log_event("nieoczekiwany komunikat %s", msg_type_name(m->type));
        break;
    }
}

static void report_lost(mqd_t ctrl, pid_t pid, int sig)
{
    struct msg m = {0};
    m.type = EV_SHIP_LOST;
    m.sender = getpid();
    m.ship = pid;
    m.arg = sig;
    ipc_mq_send(ctrl, &m);
}

/* Statek, który odleciał, kończy się kodem 0. Inne zakończenie oznacza utratę statku. */
static void reap_ships(mqd_t ctrl)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int slot = find_slot(pid);
        if (slot != -1) {
            ships[slot] = 0;
        }
        if (WIFSIGNALED(status)) {
            log_event("statek %d zabity sygnałem %d", (int)pid, WTERMSIG(status));
            report_lost(ctrl, pid, WTERMSIG(status));
        } else if (WEXITSTATUS(status) != 0) {
            log_event("statek %d zakończył się błędem, kod %d", (int)pid, WEXITSTATUS(status));
            report_lost(ctrl, pid, 0);
        }
    }
    if (pid == -1 && errno != ECHILD) {
        die("waitpid");
    }
}

static void stop_ships(void)
{
    for (int i = 0; i < SHIP_MAX; i++) {
        if (ships[i] != 0 && kill(ships[i], SIGTERM) == -1 && errno != ESRCH) {
            die("kill");
        }
    }
    for (int i = 0; i < SHIP_MAX; i++) {
        if (ships[i] == 0) {
            continue;
        }
        int status;
        while (waitpid(ships[i], &status, 0) == -1) {
            if (errno != EINTR) {
                die("waitpid");
            }
        }
        ships[i] = 0;
    }
}

int main(void)
{
    signals_setup();
    struct station_state *state = ipc_shm_attach();
    sem_t *mutex = ipc_sem_attach(NAME_MUTEX);
    log_init("generator", state, mutex);
    mqd_t inbox = ipc_mq_attach(NAME_GEN, O_RDONLY);
    mqd_t ctrl = ipc_mq_attach(NAME_CTRL, O_WRONLY);
    /* Ziarno z pid: każde uruchomienie stacji daje inny ruch. */
    srand((unsigned int)getpid());
    long long next_spawn = next_spawn_time(time_now_ms());
    log_event("start");

    while (!signals_stop_requested()) {
        struct msg m;
        if (ipc_mq_receive(inbox, &m, LOOP_TICK_MS)) {
            handle_command(&m);
        }
        long long now = time_now_ms();
        if (now >= next_spawn) {
            spawn_ship(random_type());
            next_spawn = next_spawn_time(now);
        }
        reap_ships(ctrl);
    }

    log_event("zatrzymywanie statków");
    stop_ships();
    log_event("koniec");
    log_close();
    ipc_mq_close(ctrl);
    ipc_mq_close(inbox);
    ipc_sem_close(mutex);
    ipc_shm_detach(state);
    return EXIT_SUCCESS;
}

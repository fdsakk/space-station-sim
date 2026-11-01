/* Nadzór nad procesami potomnymi stacji: uruchamianie (fork + exec), zbieranie, zatrzymanie. */
#include "supervisor.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../common/config.h"
#include "../common/die.h"
#include "../common/log.h"
#include "../common/signals.h"

#define CHILD_NAME_LEN 24

struct child {
    pid_t pid; /* 0 = wolne miejsce */
    int port;  /* indeks portu, gdy to proces portu; -1 = inny proces */
    char name[CHILD_NAME_LEN];
};

static struct child children[CHILD_COUNT];

static struct child *find_child(pid_t pid)
{
    for (int i = 0; i < CHILD_COUNT; i++) {
        if (children[i].pid == pid) {
            return &children[i];
        }
    }
    return NULL;
}

/* fork + exec programu z jednym argumentem (arg może być NULL). Zwraca miejsce dziecka. */
static struct child *spawn(const char *program, const char *arg)
{
    struct child *slot = find_child(0);
    if (slot == NULL) {
        fprintf(stderr, "supervisor: brak miejsca na proces %s\n", program);
        exit(EXIT_FAILURE);
    }
    char path[PATH_LEN];
    snprintf(path, sizeof path, "%s/%s", BIN_DIR, program);

    pid_t pid = signals_fork();
    if (pid == -1) {
        die("fork");
    }
    if (pid == 0) {
        /* Proces potomny: zastępujemy go programem. Gdy arg == NULL, lista kończy się wcześniej. */
        execl(path, program, arg, (char *)NULL);
        perror(path);
        _exit(EXIT_FAILURE);
    }
    slot->pid = pid;
    snprintf(slot->name, sizeof slot->name, "%s%s%s", program, arg ? " " : "", arg ? arg : "");
    log_event("uruchomiono %s (pid %d)", slot->name, (int)pid);
    return slot;
}

void supervisor_spawn(const char *program, const char *arg)
{
    spawn(program, arg)->port = -1;
}

void supervisor_spawn_port(int port)
{
    char arg[NAME_LEN];
    snprintf(arg, sizeof arg, "%d", port + 1); /* numer dla ludzi */
    spawn("port", arg)->port = port;
}

int supervisor_port_running(int port)
{
    for (int i = 0; i < CHILD_COUNT; i++) {
        if (children[i].pid != 0 && children[i].port == port) {
            return 1;
        }
    }
    return 0;
}

static void log_exit(const struct child *c, int status)
{
    if (WIFSIGNALED(status)) {
        log_event("%s (pid %d) zabity sygnałem %d", c->name, (int)c->pid, WTERMSIG(status));
    } else {
        log_event("%s (pid %d) zakończył się, kod %d", c->name, (int)c->pid, WEXITSTATUS(status));
    }
}

void supervisor_reap(void)
{
    int status;
    pid_t pid;
    /* WNOHANG: zbieramy tylko procesy już zakończone, bez czekania. */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        struct child *c = find_child(pid);
        if (c != NULL) {
            log_exit(c, status);
            c->pid = 0;
        }
    }
    if (pid == -1 && errno != ECHILD) {
        die("waitpid");
    }
}

void supervisor_stop_all(void)
{
    for (int i = 0; i < CHILD_COUNT; i++) {
        if (children[i].pid != 0 && kill(children[i].pid, SIGTERM) == -1 && errno != ESRCH) {
            die("kill");
        }
    }
    for (int i = 0; i < CHILD_COUNT; i++) {
        if (children[i].pid == 0) {
            continue;
        }
        int status;
        while (waitpid(children[i].pid, &status, 0) == -1) {
            if (errno != EINTR) {
                die("waitpid");
            }
        }
        log_exit(&children[i], status);
        children[i].pid = 0;
    }
}

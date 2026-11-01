/* Dziennik zdarzeń: każdy wpis trafia do pliku stacja.log i do bufora w pamięci dzielonej. */
#include "log.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "die.h"
#include "ipc.h"

#define WHO_LEN 16

static char log_who[WHO_LEN];
static struct station_state *log_state;
static sem_t *log_mutex;
static int log_fd = -1;

void log_init(const char *who, struct station_state *state, sem_t *mutex)
{
    snprintf(log_who, sizeof log_who, "%s", who);
    log_state = state;
    log_mutex = mutex;
    /* O_APPEND: każdy write trafia na koniec pliku, także gdy pisze kilka procesów naraz. */
    log_fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd == -1) {
        die("open " LOG_PATH);
    }
}

void log_close(void)
{
    if (close(log_fd) == -1) {
        die("close " LOG_PATH);
    }
    log_fd = -1;
}

void log_ring_push(struct log_ring *ring, const char *line)
{
    snprintf(ring->lines[ring->next], LOG_LINE_LEN, "%s", line);
    ring->next = (ring->next + 1) % LOG_LINES;
    if (ring->count < LOG_LINES) {
        ring->count++;
    }
}

static void format_time(char *buf, size_t len)
{
    struct timespec now;
    struct tm local;
    if (clock_gettime(CLOCK_REALTIME, &now) == -1 || localtime_r(&now.tv_sec, &local) == NULL) {
        die("czas");
    }
    char hms[16];
    strftime(hms, sizeof hms, "%H:%M:%S", &local);
    snprintf(buf, len, "%s.%03ld", hms, now.tv_nsec / 1000000L);
}

void log_event(const char *fmt, ...)
{
    char stamp[16];
    format_time(stamp, sizeof stamp);

    /* Wiersz = "czas proces treść". Treść dopisujemy za prefiksem; za długa zostaje obcięta. */
    char line[LOG_LINE_LEN];
    int prefix = snprintf(line, sizeof line, "%s %-9s ", stamp, log_who);
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + prefix, sizeof line - prefix, fmt, args);
    va_end(args);

    /* Sekcja krytyczna tylko na kopię do pamięci dzielonej; zapis do pliku poza nią. */
    ipc_lock(log_mutex);
    log_ring_push(&log_state->log, line);
    ipc_unlock(log_mutex);

    /* Jedno wywołanie write = cały wiersz naraz (wiersze różnych procesów się nie przeplatają). */
    size_t len = strlen(line);
    line[len] = '\n';
    if (write(log_fd, line, len + 1) == -1) {
        perror("write " LOG_PATH);
    }
}

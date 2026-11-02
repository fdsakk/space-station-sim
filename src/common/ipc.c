/* Tworzenie, otwieranie i usuwanie obiektów IPC stacji oraz blokada pamięci dzielonej. */
#include "ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "die.h"
#include "timeutil.h"

static struct station_state *map_state(int fd)
{
    void *addr =
        mmap(NULL, sizeof(struct station_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        die("mmap");
    }
    /* Po mmap deskryptor nie jest potrzebny - odwzorowanie zostaje. */
    if (close(fd) == -1) {
        die("close");
    }
    return addr;
}

int ipc_station_lock(void)
{
    /* Blokadę fcntl system zwalnia przy każdej śmierci procesu, także po kill -9. Dlatego wolna
     * blokada oznacza, że żadna stacja nie działa. O_CLOEXEC: dzieci nie dostają deskryptora. */
    int fd = open(LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd == -1) {
        die(LOCK_PATH);
    }
    struct flock lock = {0};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLK, &lock) == -1) {
        if (errno != EACCES && errno != EAGAIN) {
            die(LOCK_PATH);
        }
        fprintf(stderr, "station: inna stacja już działa\n");
        exit(EXIT_FAILURE);
    }
    return fd;
}

struct station_state *ipc_shm_create(void)
{
    /* O_EXCL: pozostałości usuwa start stacji, więc istniejący tu obiekt byłby błędem. */
    int fd = shm_open(NAME_SHM, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        die("shm_open " NAME_SHM);
    }
    /* ftruncate ustala rozmiar i wypełnia pamięć zerami. */
    if (ftruncate(fd, sizeof(struct station_state)) == -1) {
        die("ftruncate");
    }
    return map_state(fd);
}

struct station_state *ipc_shm_attach(void)
{
    int fd = shm_open(NAME_SHM, O_RDWR, 0);
    if (fd == -1) {
        die("shm_open " NAME_SHM);
    }
    return map_state(fd);
}

void ipc_shm_detach(struct station_state *state)
{
    if (munmap(state, sizeof *state) == -1) {
        die("munmap");
    }
}

sem_t *ipc_sem_create(const char *name, unsigned int value)
{
    sem_t *sem = sem_open(name, O_CREAT | O_EXCL, 0600, value);
    if (sem == SEM_FAILED) {
        die(name);
    }
    return sem;
}

sem_t *ipc_sem_attach(const char *name)
{
    sem_t *sem = sem_open(name, 0);
    if (sem == SEM_FAILED) {
        die(name);
    }
    return sem;
}

void ipc_sem_close(sem_t *sem)
{
    if (sem_close(sem) == -1) {
        die("sem_close");
    }
}

void ipc_lock(sem_t *mutex)
{
    /* EINTR: oczekiwanie przerwał sygnał - blokady jeszcze nie mamy, czekamy dalej. */
    while (sem_wait(mutex) == -1) {
        if (errno != EINTR) {
            die("sem_wait");
        }
    }
}

void ipc_unlock(sem_t *mutex)
{
    if (sem_post(mutex) == -1) {
        die("sem_post");
    }
}

mqd_t ipc_mq_create(const char *name, int flags)
{
    struct mq_attr attr = {0};
    attr.mq_maxmsg = QUEUE_CAPACITY;
    attr.mq_msgsize = sizeof(struct msg);
    mqd_t queue = mq_open(name, O_RDWR | O_CREAT | O_EXCL | flags, 0600, &attr);
    if (queue == (mqd_t)-1) {
        die(name);
    }
    return queue;
}

mqd_t ipc_mq_attach(const char *name, int flags)
{
    mqd_t queue = mq_open(name, flags);
    if (queue == (mqd_t)-1) {
        die(name);
    }
    return queue;
}

int ipc_mq_receive(mqd_t queue, struct msg *out, int timeout_ms)
{
    struct timespec deadline = time_deadline(timeout_ms);
    ssize_t n = mq_timedreceive(queue, (char *)out, sizeof *out, NULL, &deadline);
    if (n == -1) {
        if (errno == ETIMEDOUT || errno == EINTR) {
            return 0;
        }
        die("mq_timedreceive");
    }
    return 1;
}

void ipc_mq_send(mqd_t queue, const struct msg *m)
{
    /* EINTR: sygnał zatrzymania przerwał czekanie na miejsce w pełnej kolejce. Ponowienie
     * mogłoby czekać bez końca (np. odbiorca już nie żyje), a pętla główna i tak się kończy. */
    if (mq_send(queue, (const char *)m, sizeof *m, 0) == -1 && errno != EINTR) {
        die("mq_send");
    }
}

int ipc_mq_try_send(mqd_t queue, const struct msg *m)
{
    if (mq_send(queue, (const char *)m, sizeof *m, 0) == -1) {
        if (errno == EAGAIN) {
            return 0;
        }
        die("mq_send");
    }
    return 1;
}

void ipc_mq_close(mqd_t queue)
{
    if (mq_close(queue) == -1) {
        die("mq_close");
    }
}

void ipc_port_queue_name(int port, char *buf, size_t len)
{
    snprintf(buf, len, "%s%d", NAME_PORT_PREFIX, port);
}

void ipc_ship_queue_name(pid_t ship, char *buf, size_t len)
{
    snprintf(buf, len, "%s%d", NAME_SHIP_PREFIX, (int)ship);
}

static void report_unlink(int result, const char *name)
{
    if (result == -1 && errno != ENOENT) {
        perror(name);
    }
}

void ipc_unlink_all(void)
{
    report_unlink(shm_unlink(NAME_SHM), NAME_SHM);
    report_unlink(sem_unlink(NAME_MUTEX), NAME_MUTEX);
    report_unlink(sem_unlink(NAME_ENERGY), NAME_ENERGY);
    report_unlink(mq_unlink(NAME_CTRL), NAME_CTRL);
    report_unlink(mq_unlink(NAME_GEN), NAME_GEN);
    for (int i = 0; i < PORT_COUNT; i++) {
        char name[NAME_LEN];
        ipc_port_queue_name(i, name, sizeof name);
        report_unlink(mq_unlink(name), name);
    }
}

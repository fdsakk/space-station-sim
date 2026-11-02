/* Logika sterownika: zgłoszenia statków, przydział portów, pośredniczenie statek <-> port,
 * limit czasu potwierdzenia przydziału, utrata statku, awarie portów, komendy operatora.
 * Rekordy statków i kolejka są lokalne, a panel dostaje ich kopię raz na cykl. */
#include "control.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "../common/die.h"
#include "../common/ipc.h"
#include "../common/log.h"
#include "../common/timeutil.h"
#include "../port/port_fsm.h"
#include "queue.h"
#include "supervisor.h"

struct ship {
    struct ship_record rec;    /* część widoczna w pamięci dzielonej */
    mqd_t queue;               /* kolejka statku, otwarta z O_NONBLOCK */
    long long since_ms;        /* chwila zgłoszenia: z nią statek wraca do kolejki po awarii */
    long long ack_deadline_ms; /* do kiedy czekamy na SHIP_ACK_ASSIGN */
};

static struct station_state *ctl_state;
static sem_t *ctl_mutex;
static sem_t *ctl_energy;
static const mqd_t *ctl_ports;
static mqd_t ctl_gen;
static struct ship ships[SHIP_MAX];
static struct ship_queue queue;
static int port_down[PORT_COUNT]; /* 1 = proces portu nie żyje, a jego rekord jest w AWARIA */

/* find_ship(0) zwraca wolne miejsce. */
static struct ship *find_ship(pid_t pid)
{
    for (int i = 0; i < SHIP_MAX; i++) {
        if (ships[i].rec.pid == pid) {
            return &ships[i];
        }
    }
    return NULL;
}

static void set_state(struct ship *s, enum ship_state state)
{
    log_event("statek %d: %s -> %s", (int)s->rec.pid, ship_state_name(s->rec.state),
              ship_state_name(state));
    s->rec.state = state;
}

static struct msg make_msg(enum msg_type type, pid_t ship, int port)
{
    struct msg m = {0};
    m.type = type;
    m.sender = getpid();
    m.ship = ship;
    m.port = port;
    return m;
}

/* Kolejki portów i generatora mają O_NONBLOCK. Pełna kolejka oznacza, że odbiorca jest
 * zawieszony albo zalewany komendami: polecenie przepada z wpisem w dzienniku. */
static void send_to_port(int port, enum msg_type type, pid_t ship)
{
    struct msg m = make_msg(type, ship, port);
    if (!ipc_mq_try_send(ctl_ports[port], &m)) {
        log_event("port %d nie odbiera (kolejka pełna) - pominięto %s", port + 1,
                  msg_type_name(type));
    }
}

/* Zwalnia rekord i to, co statek zajmował: miejsce w kolejce, rezerwację albo port. */
static void release_ship(struct ship *s, enum ship_state final_state)
{
    switch (s->rec.state) {
    case SHIP_WAITING:
        queue_remove(&queue, s->rec.pid);
        break;
    case SHIP_ASSIGNED:
        send_to_port(s->rec.port, CMD_CANCEL, s->rec.pid);
        break;
    case SHIP_DOCKING:
    case SHIP_DOCKED:
        send_to_port(s->rec.port, CMD_UNDOCK, s->rec.pid);
        break;
    case SHIP_UNDOCKING:
    case SHIP_DEPARTED:
    case SHIP_LOST:
        break; /* port sam kończy oddokowanie */
    }
    set_state(s, final_state);
    ipc_mq_close(s->queue);
    s->rec.pid = 0;
}

/* Statek, który nie odbiera albo nie odpowiada, usuwamy z systemu i kończymy jego proces
 * sygnałem: komunikatu i tak by nie przeczytał, a zajmowałby miejsce w limicie statków. */
static void expel_ship(struct ship *s)
{
    pid_t pid = s->rec.pid;
    release_ship(s, SHIP_LOST);
    if (kill(pid, SIGTERM) == -1 && errno != ESRCH) {
        log_event("kill %d: %s", (int)pid, strerror(errno));
    }
}

/* Zapis do statku nie blokuje (sterownik nie może zawisnąć na wolnym statku). Zwraca 0, gdy statek
 * nie odbiera - wtedy jest już usunięty z systemu i wywołujący nie może używać s. */
static int send_to_ship(struct ship *s, enum msg_type type)
{
    struct msg m = make_msg(type, s->rec.pid, s->rec.port);
    if (ipc_mq_try_send(s->queue, &m)) {
        return 1;
    }
    log_event("statek %d nie odbiera komunikatów", (int)s->rec.pid);
    expel_ship(s);
    return 0;
}

static void on_hello(const struct msg *m)
{
    char name[NAME_LEN];
    ipc_ship_queue_name(m->sender, name, sizeof name);
    mqd_t q = mq_open(name, O_WRONLY | O_NONBLOCK);
    if (q == (mqd_t)-1) {
        log_event("statek %d: brak kolejki %s", (int)m->sender, name);
        return;
    }
    struct ship *s = find_ship(0);
    if (s == NULL) {
        /* Więcej statków niż SHIP_MAX (np. uruchomionych ręcznie): odsyłamy od razu. */
        log_event("brak miejsca dla statku %d - odesłany", (int)m->sender);
        struct msg reply = make_msg(DEPART_OK, m->sender, -1);
        if (!ipc_mq_try_send(q, &reply)) {
            log_event("statek %d nie odbiera komunikatów", (int)m->sender);
        }
        ipc_mq_close(q);
        return;
    }
    s->rec.pid = m->sender;
    s->rec.type = (enum ship_type)m->ship_type;
    s->rec.state = SHIP_WAITING;
    s->rec.port = -1;
    s->queue = q;
    s->since_ms = time_now_ms();
    struct queue_entry e = {s->rec.pid, s->rec.type, s->since_ms};
    queue_push(&queue, e);
    log_event("statek %d (%s) zgłosił się, w kolejce: %d", (int)s->rec.pid,
              ship_type_name(s->rec.type), queue.count);
}

static void on_ack(const struct msg *m)
{
    struct ship *s = find_ship(m->sender);
    if (s == NULL || s->rec.state != SHIP_ASSIGNED) {
        log_event("spóźnione potwierdzenie od statku %d - pominięte", (int)m->sender);
        return;
    }
    set_state(s, SHIP_DOCKING);
    send_to_port(s->rec.port, CMD_DOCK, s->rec.pid);
}

static void on_undock_request(const struct msg *m)
{
    struct ship *s = find_ship(m->sender);
    if (s == NULL || s->rec.state != SHIP_DOCKED) {
        log_event("prośba o oddokowanie od statku %d nie w porcie - pominięta", (int)m->sender);
        return;
    }
    set_state(s, SHIP_UNDOCKING);
    send_to_port(s->rec.port, CMD_UNDOCK, s->rec.pid);
}

/* Statek, którego dotyczy zdarzenie portu; NULL, gdy już go nie ma (np. został utracony). */
static struct ship *ship_of_event(const struct msg *m)
{
    struct ship *s = find_ship(m->ship);
    if (s == NULL) {
        log_event("%s z portu %d: statku %d już nie ma", msg_type_name(m->type), m->port + 1,
                  (int)m->ship);
    }
    return s;
}

static void on_docked(const struct msg *m)
{
    struct ship *s = ship_of_event(m);
    if (s != NULL) {
        set_state(s, SHIP_DOCKED);
        send_to_ship(s, DOCKED);
    }
}

static void on_service_done(const struct msg *m)
{
    struct ship *s = ship_of_event(m);
    if (s != NULL) {
        send_to_ship(s, SERVICE_DONE);
    }
}

static void depart(struct ship *s)
{
    if (send_to_ship(s, DEPART_OK)) {
        release_ship(s, SHIP_DEPARTED);
    }
}

static void on_undocked(const struct msg *m)
{
    struct ship *s = ship_of_event(m);
    if (s != NULL) {
        depart(s);
    }
}

static void forget_lost_ship(pid_t pid)
{
    struct ship *s = find_ship(pid);
    if (s != NULL) {
        release_ship(s, SHIP_LOST);
    }
    /* Martwy statek nie usunie swojej kolejki, więc robi to sterownik. */
    char name[NAME_LEN];
    ipc_ship_queue_name(pid, name, sizeof name);
    if (mq_unlink(name) == -1 && errno != ENOENT) {
        log_event("mq_unlink %s: %s", name, strerror(errno));
    }
}

static void on_ship_lost(const struct msg *m)
{
    log_event("statek %d utracony (sygnał %d)", (int)m->ship, m->arg);
    forget_lost_ship(m->ship);
}

/* Statek z zepsutego portu wraca na początek kolejki w swojej grupie priorytetu.
 * Zachowuje czas zgłoszenia, żeby ochrona przed zagłodzeniem liczyła całe czekanie. */
static void requeue(struct ship *s)
{
    log_event("statek %d wraca do kolejki (awaria portu %d)", (int)s->rec.pid, s->rec.port + 1);
    set_state(s, SHIP_WAITING);
    s->rec.port = -1;
    struct queue_entry e = {s->rec.pid, s->rec.type, s->since_ms};
    queue_push_front(&queue, e);
    send_to_ship(s, PORT_FAULT);
}

/* Awaria opróżnia port. Statek w trakcie odlotu odlatuje od razu, bo port już go nie trzyma,
 * a każdy inny wraca na początek kolejki i dostanie inny port. */
static void on_fault(const struct msg *m)
{
    if (m->ship == 0) {
        return;
    }
    struct ship *s = ship_of_event(m);
    if (s == NULL) {
        return;
    }
    if (s->rec.state == SHIP_UNDOCKING) {
        depart(s);
    } else {
        requeue(s);
    }
}

static void send_to_generator(enum msg_type type, int ship_type)
{
    struct msg m = make_msg(type, 0, -1);
    m.ship_type = ship_type;
    if (!ipc_mq_try_send(ctl_gen, &m)) {
        log_event("generator nie odbiera (kolejka pełna) - pominięto %s", msg_type_name(type));
    }
}

static void on_operator_fault(const struct msg *m)
{
    log_event("operator: awaria portu %d", m->port + 1);
    if (port_down[m->port]) {
        /* Nikt nie czyta kolejki martwego portu, więc nie dokładamy do niej poleceń. */
        log_event("port %d: proces nie działa, port już jest w awarii", m->port + 1);
        return;
    }
    send_to_port(m->port, CMD_FAULT, 0);
}

static void on_operator_repair(const struct msg *m)
{
    log_event("operator: naprawa portu %d", m->port + 1);
    if (port_down[m->port]) {
        /* Nowy proces pracuje na tym samym rekordzie (stan AWARIA): wykona polecenia, które
         * czekają w jego kolejce, a CMD_REPAIR uruchomi naprawę. */
        supervisor_spawn_port(m->port);
        port_down[m->port] = 0;
    }
    send_to_port(m->port, CMD_REPAIR, 0);
}

void control_init(struct station_state *state, sem_t *mutex, sem_t *energy,
                  const mqd_t ports[PORT_COUNT], mqd_t gen)
{
    ctl_state = state;
    ctl_mutex = mutex;
    ctl_energy = energy;
    ctl_ports = ports;
    ctl_gen = gen;
}

int control_handle(const struct msg *m)
{
    switch (m->type) {
    case SHIP_HELLO:
        on_hello(m);
        break;
    case SHIP_ACK_ASSIGN:
        on_ack(m);
        break;
    case SHIP_UNDOCK_REQ:
        on_undock_request(m);
        break;
    case EV_DOCKED:
        on_docked(m);
        break;
    case EV_SERVICE_DONE:
        on_service_done(m);
        break;
    case EV_UNDOCKED:
        on_undocked(m);
        break;
    case EV_SHIP_LOST:
        on_ship_lost(m);
        break;
    case EV_FAULT:
        on_fault(m);
        break;
    case OP_FAULT:
        on_operator_fault(m);
        break;
    case OP_REPAIR:
        on_operator_repair(m);
        break;
    case OP_SPAWN:
        log_event("operator: statek %s", ship_type_name((enum ship_type)m->ship_type));
        send_to_generator(CMD_SPAWN, m->ship_type);
        break;
    case OP_WAVE:
        log_event("operator: fala");
        send_to_generator(CMD_WAVE, 0);
        break;
    case OP_SHUTDOWN:
        log_event("operator: zatrzymanie stacji");
        return 0;
    default:
        log_event("pominięto %s od pid %d", msg_type_name(m->type), (int)m->sender);
        break;
    }
    return 1;
}

static void check_ack_timeouts(long long now_ms)
{
    for (int i = 0; i < SHIP_MAX; i++) {
        struct ship *s = &ships[i];
        if (s->rec.pid != 0 && s->rec.state == SHIP_ASSIGNED && now_ms >= s->ack_deadline_ms) {
            log_event("statek %d nie potwierdził przydziału portu %d w %d ms - rezerwacja "
                      "anulowana",
                      (int)s->rec.pid, s->rec.port + 1, ACK_TIMEOUT_MS);
            expel_ship(s);
        }
    }
}

/* Generator zgłasza tylko swoje statki zabite sygnałem albo zakończone błędem. Statek po SIGTERM
 * kończy się kodem 0, a uruchomionego ręcznie generator nie zna, więc sterownik sprawdza sam:
 * kill z sygnałem 0 niczego nie wysyła, tylko mówi, czy proces istnieje. */
static void check_ships_alive(void)
{
    for (int i = 0; i < SHIP_MAX; i++) {
        pid_t pid = ships[i].rec.pid;
        if (pid != 0 && kill(pid, 0) == -1 && errno == ESRCH) {
            log_event("statek %d zniknął bez zgłoszenia", (int)pid);
            forget_lost_ship(pid);
        }
    }
}

/* Proces portu zginął: rekord martwego portu pisze sterownik. Wykonuje na nim
 * to samo przejście automatu, co port po CMD_FAULT, i oddaje energię, którą port trzymał. */
static void port_died(int port, long long now_ms)
{
    struct msg cmd = make_msg(CMD_FAULT, 0, port);
    ipc_lock(ctl_mutex);
    struct port_action a = port_fsm_command(&ctl_state->ports[port], &cmd, now_ms);
    /* Flaga "trzyma energię" i semafor zmieniają się w tej samej sekcji krytycznej. */
    int post_result = a.release_energy ? sem_post(ctl_energy) : 0;
    ipc_unlock(ctl_mutex);
    if (post_result == -1) {
        die("sem_post"); /* dopiero po zwolnieniu mutexu, żeby nie zablokować stacji */
    }
    port_down[port] = 1;
    log_event("port %d: proces nie działa - port w awarii%s", port + 1,
              a.release_energy ? ", energia odzyskana" : "");
    if (a.send) {
        struct msg fault = make_msg(EV_FAULT, a.ship, port);
        on_fault(&fault);
    }
}

static void check_ports(long long now_ms)
{
    for (int port = 0; port < PORT_COUNT; port++) {
        if (!port_down[port] && !supervisor_port_running(port)) {
            port_died(port, now_ms);
        }
    }
}

/* Sprawdzenie "port wolny" i rezerwacja w jednej sekcji krytycznej: port nie może dostać
 * dwóch statków. */
static int reserve_port(int port, long long now_ms, struct queue_entry *out)
{
    ipc_lock(ctl_mutex);
    struct port_record *p = &ctl_state->ports[port];
    int is_free = p->state == PORT_FREE;
    if (is_free) {
        *out = queue_pop(&queue, now_ms);
        port_fsm_reserve(p, out->pid, out->type);
    }
    ipc_unlock(ctl_mutex);
    return is_free;
}

static void assign_ports(long long now_ms)
{
    for (int port = 0; port < PORT_COUNT && queue.count > 0; port++) {
        struct queue_entry e;
        if (!reserve_port(port, now_ms, &e)) {
            continue;
        }
        struct ship *s = find_ship(e.pid);
        s->rec.port = port;
        s->ack_deadline_ms = now_ms + ACK_TIMEOUT_MS;
        log_event("port %d przydzielony statkowi %d", port + 1, (int)e.pid);
        set_state(s, SHIP_ASSIGNED);
        send_to_ship(s, ASSIGNED);
    }
}

static void publish(void)
{
    ipc_lock(ctl_mutex);
    for (int i = 0; i < SHIP_MAX; i++) {
        ctl_state->ships[i] = ships[i].rec;
    }
    ctl_state->queue = queue;
    ipc_unlock(ctl_mutex);
}

void control_tick(void)
{
    long long now = time_now_ms();
    check_ports(now);
    check_ships_alive();
    check_ack_timeouts(now);
    assign_ports(now);
    publish();
}

void control_close(void)
{
    for (int i = 0; i < SHIP_MAX; i++) {
        if (ships[i].rec.pid != 0) {
            ipc_mq_close(ships[i].queue);
        }
    }
}

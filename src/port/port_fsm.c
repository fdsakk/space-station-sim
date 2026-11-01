/* Automat portu dokującego razem ze śluzą i obsługą.
 * Czyste funkcje bez IPC: wejście (rekord, polecenie albo upływ czasu) -> nowy rekord i akcje. */
#include "port_fsm.h"

#include "../common/config.h"

static struct port_action no_action(void)
{
    struct port_action a = {0};
    return a;
}

static struct port_action event(enum msg_type type, pid_t ship)
{
    struct port_action a = no_action();
    a.send = 1;
    a.event = type;
    a.ship = ship;
    return a;
}

static struct port_action reject(const struct msg *cmd)
{
    struct port_action a = no_action();
    a.ship = cmd->ship;
    a.rejected = 1;
    a.rejected_type = (enum msg_type)cmd->type;
    return a;
}

/* Flaga "trzyma energię" i sem_post muszą zmienić się w tej samej sekcji krytycznej
 * (liczba portów z flagą + wolne jednostki = wszystkie jednostki), więc automat tylko zleca
 * oddanie energii. */
static void drop_energy(struct port_record *p, struct port_action *a)
{
    if (p->has_energy) {
        p->has_energy = 0;
        a->release_energy = 1;
    }
}

static long long service_ms(enum ship_type type)
{
    switch (type) {
    case SHIP_TYPE_SHUTTLE:
        return SERVICE_SHUTTLE_MS;
    case SHIP_TYPE_FREIGHTER:
        return SERVICE_FREIGHTER_MS;
    case SHIP_TYPE_EMERGENCY:
        return SERVICE_EMERGENCY_MS;
    }
    return SERVICE_FREIGHTER_MS;
}

void port_fsm_reserve(struct port_record *p, pid_t ship, enum ship_type type)
{
    p->state = PORT_RESERVED;
    p->ship = ship;
    p->ship_type = type;
}

static struct port_action dock(struct port_record *p, const struct msg *cmd, long long now_ms)
{
    if (p->state != PORT_RESERVED || cmd->ship != p->ship) {
        return reject(cmd);
    }
    p->state = PORT_DOCKING;
    p->timer_end_ms = now_ms + DOCK_MS;
    return no_action();
}

static struct port_action cancel(struct port_record *p, const struct msg *cmd)
{
    /* Jak w dock: anulowanie dla innego statku (spóźnione) nie zabiera cudzej rezerwacji. */
    if (p->state != PORT_RESERVED || cmd->ship != p->ship) {
        return reject(cmd);
    }
    p->state = PORT_FREE;
    p->ship = 0;
    return no_action();
}

/* Oddokowanie na prośbę statku albo po jego utracie, także w trakcie dokowania lub obsługi:
 * najpierw stan bezpieczny (obsługa przerwana, energia oddana, śluza zamknięta). */
static struct port_action undock(struct port_record *p, const struct msg *cmd, long long now_ms)
{
    if (p->state != PORT_DOCKING && p->state != PORT_DOCKED) {
        return reject(cmd);
    }
    struct port_action a = no_action();
    drop_energy(p, &a);
    p->service = SERVICE_NONE;
    p->lock = LOCK_CLOSED;
    p->state = PORT_UNDOCKING;
    p->timer_end_ms = now_ms + UNDOCK_MS;
    return a;
}

static struct port_action fault(struct port_record *p, const struct msg *cmd)
{
    if (p->state == PORT_BROKEN) {
        return reject(cmd);
    }
    /* Awaria opróżnia port: stan bezpieczny, a statek wraca do kolejki sterownika. */
    struct port_action a = event(EV_FAULT, p->ship);
    drop_energy(p, &a);
    p->service = SERVICE_NONE;
    p->lock = LOCK_CLOSED;
    p->ship = 0;
    p->state = PORT_BROKEN;
    return a;
}

static struct port_action repair(struct port_record *p, const struct msg *cmd)
{
    if (p->state != PORT_BROKEN) {
        return reject(cmd);
    }
    p->state = PORT_REPAIRING; /* najpierw czeka na energię (port_fsm_needs_energy) */
    return no_action();
}

struct port_action port_fsm_command(struct port_record *p, const struct msg *cmd, long long now_ms)
{
    switch (cmd->type) {
    case CMD_DOCK:
        return dock(p, cmd, now_ms);
    case CMD_UNDOCK:
        return undock(p, cmd, now_ms);
    case CMD_FAULT:
        return fault(p, cmd);
    case CMD_REPAIR:
        return repair(p, cmd);
    case CMD_CANCEL:
        return cancel(p, cmd);
    default:
        /* Komunikat, który nie jest poleceniem dla portu. */
        return reject(cmd);
    }
}

/* Podrzędne automaty stanu ZADOKOWANY: śluza i obsługa. */
static struct port_action docked_tick(struct port_record *p, long long now_ms)
{
    if (p->lock == LOCK_CLOSED && p->service == SERVICE_NONE) {
        p->lock = LOCK_EQUALIZING;
        p->timer_end_ms = now_ms + EQUALIZE_MS;
    } else if (p->lock == LOCK_EQUALIZING && now_ms >= p->timer_end_ms) {
        p->lock = LOCK_OPEN;
        p->service = SERVICE_WAITING_ENERGY;
    } else if (p->service == SERVICE_RUNNING && now_ms >= p->timer_end_ms) {
        struct port_action a = no_action();
        drop_energy(p, &a);
        p->service = SERVICE_FINISHED;
        p->lock = LOCK_CLOSING;
        p->timer_end_ms = now_ms + LOCK_CLOSE_MS;
        return a;
    } else if (p->lock == LOCK_CLOSING && now_ms >= p->timer_end_ms) {
        /* Zgłaszamy koniec obsługi dopiero przy zamkniętej śluzie: wtedy statek może prosić
         * o oddokowanie, a oddokowanie wymaga zamkniętej śluzy. */
        p->lock = LOCK_CLOSED;
        return event(EV_SERVICE_DONE, p->ship);
    }
    return no_action();
}

static struct port_action repairing_tick(struct port_record *p, long long now_ms)
{
    if (!p->has_energy || now_ms < p->timer_end_ms) {
        return no_action();
    }
    struct port_action a = no_action();
    drop_energy(p, &a);
    p->state = PORT_FREE;
    return a;
}

struct port_action port_fsm_tick(struct port_record *p, long long now_ms)
{
    switch (p->state) {
    case PORT_FREE:
    case PORT_RESERVED:
    case PORT_BROKEN:
        return no_action();
    case PORT_DOCKING:
        if (now_ms < p->timer_end_ms) {
            return no_action();
        }
        p->state = PORT_DOCKED;
        return event(EV_DOCKED, p->ship);
    case PORT_DOCKED:
        return docked_tick(p, now_ms);
    case PORT_UNDOCKING: {
        if (now_ms < p->timer_end_ms) {
            return no_action();
        }
        struct port_action a = event(EV_UNDOCKED, p->ship);
        p->state = PORT_FREE;
        p->ship = 0;
        return a;
    }
    case PORT_REPAIRING:
        return repairing_tick(p, now_ms);
    }
    return no_action();
}

int port_fsm_needs_energy(const struct port_record *p)
{
    return (p->state == PORT_DOCKED && p->service == SERVICE_WAITING_ENERGY) ||
           (p->state == PORT_REPAIRING && !p->has_energy);
}

void port_fsm_energy_taken(struct port_record *p, long long now_ms)
{
    p->has_energy = 1;
    if (p->state == PORT_REPAIRING) {
        p->timer_end_ms = now_ms + REPAIR_MS;
    } else {
        p->service = SERVICE_RUNNING;
        p->timer_end_ms = now_ms + service_ms(p->ship_type);
    }
}

const char *port_state_name(enum port_state state)
{
    switch (state) {
    case PORT_FREE:
        return "WOLNY";
    case PORT_RESERVED:
        return "ZAREZERWOWANY";
    case PORT_DOCKING:
        return "DOKOWANIE";
    case PORT_DOCKED:
        return "ZADOKOWANY";
    case PORT_UNDOCKING:
        return "ODDOKOWANIE";
    case PORT_BROKEN:
        return "AWARIA";
    case PORT_REPAIRING:
        return "NAPRAWA";
    }
    return "NIEZNANY";
}

const char *lock_state_name(enum lock_state state)
{
    switch (state) {
    case LOCK_CLOSED:
        return "ZAMKNIETA";
    case LOCK_EQUALIZING:
        return "WYROWNYWANIE";
    case LOCK_OPEN:
        return "OTWARTA";
    case LOCK_CLOSING:
        return "ZAMYKANIE";
    }
    return "NIEZNANY";
}

const char *service_state_name(enum service_state state)
{
    switch (state) {
    case SERVICE_NONE:
        return "BRAK";
    case SERVICE_WAITING_ENERGY:
        return "CZEKA_NA_ENERGIE";
    case SERVICE_RUNNING:
        return "TRWA";
    case SERVICE_FINISHED:
        return "ZAKONCZONA";
    }
    return "NIEZNANY";
}

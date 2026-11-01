/* Testy automatu portu: przejścia stanów i warunki, które muszą zachodzić po każdym kroku.
 * Symulujemy trzy porty i wspólną pulę energii tak, jak robi to proces portu. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/port/port_fsm.h"

struct sim {
    struct port_record ports[PORT_COUNT];
    int pool; /* wolne jednostki energii (w programie: wartość semafora /ss_energy) */
    long long now;
};

static void check_invariants(const struct sim *s)
{
    int held = 0;
    for (int i = 0; i < PORT_COUNT; i++) {
        const struct port_record *p = &s->ports[i];
        assert(p->lock == LOCK_CLOSED ||
               p->state == PORT_DOCKED); /* śluza otwarta tylko przy statku */
        assert(p->state != PORT_UNDOCKING ||
               p->lock == LOCK_CLOSED);                    /* odlot przy zamkniętej śluzie */
        assert(p->state != PORT_BROKEN || !p->has_energy); /* awaria oddaje energię */
        assert(p->service != SERVICE_RUNNING || p->has_energy);
        held += p->has_energy;
    }
    assert(held + s->pool == ENERGY_UNITS); /* energia nie wycieka */
}

/* To samo, co robi pętla portu po kroku automatu: oddanie i pobranie energii. */
static struct port_action finish(struct sim *s, int port, struct port_action a)
{
    struct port_record *p = &s->ports[port];
    if (a.release_energy) {
        s->pool++;
    }
    if (port_fsm_needs_energy(p) && s->pool > 0) {
        s->pool--;
        port_fsm_energy_taken(p, s->now);
    }
    check_invariants(s);
    return a;
}

static struct port_action command(struct sim *s, int port, enum msg_type type, pid_t ship)
{
    struct msg cmd = {0};
    cmd.type = type;
    cmd.ship = ship;
    return finish(s, port, port_fsm_command(&s->ports[port], &cmd, s->now));
}

static struct port_action tick(struct sim *s, int port)
{
    return finish(s, port, port_fsm_tick(&s->ports[port], s->now));
}

static void sim_init(struct sim *s)
{
    memset(s, 0, sizeof *s);
    s->pool = ENERGY_UNITS;
    s->now = 1000;
}

/* Rezerwacja, dokowanie i wyrównanie ciśnienia: kończy się otwarciem śluzy. */
static void dock_ship(struct sim *s, int port, pid_t ship, enum ship_type type)
{
    port_fsm_reserve(&s->ports[port], ship, type);
    struct port_action a = command(s, port, CMD_DOCK, ship);
    assert(!a.send);
    s->now += DOCK_MS;
    a = tick(s, port);
    assert(a.send && a.event == EV_DOCKED && a.ship == ship);
    tick(s, port);
    assert(s->ports[port].lock == LOCK_EQUALIZING);
    s->now += EQUALIZE_MS;
    tick(s, port);
    assert(s->ports[port].lock == LOCK_OPEN);
}

static void test_full_cycle(void)
{
    struct sim s;
    sim_init(&s);
    dock_ship(&s, 0, 42, SHIP_TYPE_SHUTTLE);
    assert(s.ports[0].service == SERVICE_RUNNING && s.pool == ENERGY_UNITS - 1);

    s.now += SERVICE_SHUTTLE_MS - 1;
    tick(&s, 0);
    assert(s.ports[0].service == SERVICE_RUNNING);
    s.now += 1;
    tick(&s, 0);
    assert(s.ports[0].service == SERVICE_FINISHED && s.ports[0].lock == LOCK_CLOSING);
    assert(s.pool == ENERGY_UNITS);

    s.now += LOCK_CLOSE_MS;
    struct port_action a = tick(&s, 0);
    assert(a.send && a.event == EV_SERVICE_DONE && s.ports[0].lock == LOCK_CLOSED);

    a = command(&s, 0, CMD_UNDOCK, 42);
    assert(!a.send && s.ports[0].state == PORT_UNDOCKING);
    s.now += UNDOCK_MS;
    a = tick(&s, 0);
    assert(a.send && a.event == EV_UNDOCKED && a.ship == 42);
    assert(s.ports[0].state == PORT_FREE && s.ports[0].ship == 0);
}

static void test_dock_rejected(void)
{
    struct sim s;
    sim_init(&s);
    struct port_action a = command(&s, 0, CMD_DOCK, 42); /* port nie jest zarezerwowany */
    assert(a.rejected && a.rejected_type == CMD_DOCK && a.ship == 42 && !a.send);

    port_fsm_reserve(&s.ports[0], 42, SHIP_TYPE_SHUTTLE);
    a = command(&s, 0, CMD_DOCK, 7); /* rezerwacja dla innego statku */
    assert(a.rejected && s.ports[0].state == PORT_RESERVED);
}

static void test_cancel(void)
{
    struct sim s;
    sim_init(&s);
    port_fsm_reserve(&s.ports[0], 42, SHIP_TYPE_SHUTTLE);
    /* Spóźnione anulowanie dla innego statku nie zabiera rezerwacji (jak przy CMD_DOCK). */
    struct port_action a = command(&s, 0, CMD_CANCEL, 7);
    assert(a.rejected && s.ports[0].state == PORT_RESERVED && s.ports[0].ship == 42);
    a = command(&s, 0, CMD_CANCEL, 42);
    assert(!a.send && s.ports[0].state == PORT_FREE && s.ports[0].ship == 0);
    a = command(&s, 0, CMD_CANCEL, 42);
    assert(a.rejected);
}

static void test_rejects_impossible_commands(void)
{
    struct sim s;
    sim_init(&s);
    const enum msg_type impossible[] = {CMD_UNDOCK, CMD_REPAIR, SHIP_HELLO};
    for (size_t i = 0; i < sizeof impossible / sizeof impossible[0]; i++) {
        struct port_action a = command(&s, 0, impossible[i], 42);
        assert(a.rejected && a.rejected_type == impossible[i]);
    }
    assert(s.ports[0].state == PORT_FREE);
}

/* Trzy porty, dwie jednostki energii: trzeci czeka, aż któryś skończy obsługę. */
static void test_energy_shared_by_three_ports(void)
{
    struct sim s;
    sim_init(&s);
    for (int i = 0; i < PORT_COUNT; i++) {
        dock_ship(&s, i, 40 + i, SHIP_TYPE_FREIGHTER);
    }
    assert(s.ports[0].service == SERVICE_RUNNING && s.ports[1].service == SERVICE_RUNNING);
    assert(s.ports[2].service == SERVICE_WAITING_ENERGY && s.pool == 0);

    s.now += SERVICE_FREIGHTER_MS;
    tick(&s, 2);
    assert(s.ports[2].service == SERVICE_WAITING_ENERGY); /* nikt jeszcze nie oddał */
    tick(&s, 0);
    assert(s.ports[0].service == SERVICE_FINISHED && s.pool == 1);
    tick(&s, 2);
    assert(s.ports[2].service == SERVICE_RUNNING && s.pool == 0);
}

static void test_undock_during_service_makes_port_safe(void)
{
    struct sim s;
    sim_init(&s);
    dock_ship(&s, 0, 42, SHIP_TYPE_FREIGHTER);
    assert(s.ports[0].service == SERVICE_RUNNING);

    command(&s, 0, CMD_UNDOCK, 42); /* statek utracony w trakcie obsługi */
    assert(s.ports[0].state == PORT_UNDOCKING && s.ports[0].lock == LOCK_CLOSED);
    assert(s.ports[0].service == SERVICE_NONE && s.pool == ENERGY_UNITS);
}

static void test_fault_empties_port_and_repair_frees_it(void)
{
    struct sim s;
    sim_init(&s);
    dock_ship(&s, 0, 42, SHIP_TYPE_SHUTTLE);

    struct port_action a = command(&s, 0, CMD_FAULT, 0);
    assert(a.send && a.event == EV_FAULT && a.ship == 42);
    assert(s.ports[0].state == PORT_BROKEN && s.ports[0].lock == LOCK_CLOSED);
    assert(s.ports[0].service == SERVICE_NONE && s.ports[0].ship == 0);
    assert(s.pool == ENERGY_UNITS);
    a = command(&s, 0, CMD_FAULT, 0);
    assert(a.rejected);

    a = command(&s, 0, CMD_REPAIR, 0);
    assert(!a.send);
    assert(s.ports[0].state == PORT_REPAIRING && s.ports[0].has_energy);
    s.now += REPAIR_MS;
    a = tick(&s, 0);
    assert(!a.send && s.ports[0].state == PORT_FREE && s.pool == ENERGY_UNITS);
}

static void test_fault_drops_reservation(void)
{
    struct sim s;
    sim_init(&s);
    port_fsm_reserve(&s.ports[0], 42, SHIP_TYPE_SHUTTLE);
    struct port_action a = command(&s, 0, CMD_FAULT, 0);
    assert(a.event == EV_FAULT && a.ship == 42 && s.ports[0].ship == 0);

    command(&s, 0, CMD_REPAIR, 0);
    s.now += REPAIR_MS;
    tick(&s, 0);
    assert(s.ports[0].state == PORT_FREE);
}

static void test_fault_during_undocking_reports_ship(void)
{
    struct sim s;
    sim_init(&s);
    dock_ship(&s, 0, 42, SHIP_TYPE_SHUTTLE);
    command(&s, 0, CMD_UNDOCK, 42);
    struct port_action a = command(&s, 0, CMD_FAULT, 0);
    assert(a.event == EV_FAULT && a.ship == 42 && s.ports[0].state == PORT_BROKEN);
}

static void test_fault_during_repair_returns_energy(void)
{
    struct sim s;
    sim_init(&s);
    command(&s, 0, CMD_FAULT, 0);
    command(&s, 0, CMD_REPAIR, 0);
    assert(s.pool == ENERGY_UNITS - 1);
    command(&s, 0, CMD_FAULT, 0);
    assert(s.ports[0].state == PORT_BROKEN && s.pool == ENERGY_UNITS);
}

/* Spóźnione polecenie dla statku, który zniknął z portu razem z awarią. */
static void test_undock_rejected_when_broken_or_repairing(void)
{
    struct sim s;
    sim_init(&s);
    dock_ship(&s, 0, 42, SHIP_TYPE_SHUTTLE);
    command(&s, 0, CMD_FAULT, 0);
    struct port_action a = command(&s, 0, CMD_UNDOCK, 42);
    assert(a.rejected && a.rejected_type == CMD_UNDOCK);
    command(&s, 0, CMD_REPAIR, 0);
    a = command(&s, 0, CMD_UNDOCK, 42);
    assert(a.rejected && s.ports[0].state == PORT_REPAIRING);
}

int main(void)
{
    test_full_cycle();
    test_dock_rejected();
    test_cancel();
    test_rejects_impossible_commands();
    test_energy_shared_by_three_ports();
    test_undock_during_service_makes_port_safe();
    test_fault_empties_port_and_repair_frees_it();
    test_fault_drops_reservation();
    test_fault_during_undocking_reports_ship();
    test_fault_during_repair_returns_energy();
    test_undock_rejected_when_broken_or_repairing();
    puts("test_port_fsm: OK");
    return 0;
}

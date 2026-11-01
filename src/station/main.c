/* Sterownik stacji (proces główny): tworzy obiekty IPC, uruchamia procesy stacji,
 * obsługuje komunikaty i sprząta przy zatrzymaniu. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/config.h"
#include "../common/ipc.h"
#include "../common/log.h"
#include "../common/signals.h"
#include "control.h"
#include "supervisor.h"

int main(int argc, char *argv[])
{
    /* Adres, na którym brama nasłuchuje; 0.0.0.0 przyjmuje pakiety z innych komputerów. */
    if (argc > 2) {
        fprintf(stderr, "użycie: station [adres bramy, domyślnie %s]\n", GATEWAY_ADDR);
        return EXIT_FAILURE;
    }
    const char *gateway_addr = argc == 2 ? argv[1] : GATEWAY_ADDR;

    struct station_state *state = ipc_shm_create();
    sem_t *mutex = ipc_sem_create(NAME_MUTEX, 1);
    sem_t *energy = ipc_sem_create(NAME_ENERGY, ENERGY_UNITS);
    mqd_t ctrl = ipc_mq_create(NAME_CTRL, 0);
    /* Do portów i generatora sterownik tylko wysyła, bez czekania: port czekający na
     * miejsce w /ss_ctrl i sterownik czekający na miejsce w kolejce portu zablokowałyby się
     * nawzajem, a zawieszony port zatrzymałby całą stację. */
    mqd_t gen = ipc_mq_create(NAME_GEN, O_NONBLOCK);
    mqd_t ports[PORT_COUNT];
    for (int i = 0; i < PORT_COUNT; i++) {
        char name[NAME_LEN];
        ipc_port_queue_name(i, name, sizeof name);
        ports[i] = ipc_mq_create(name, O_NONBLOCK);
    }

    signals_setup();
    log_init("station", state, mutex);
    control_init(state, mutex, energy, ports, gen);
    log_event("start stacji");

    for (int i = 0; i < PORT_COUNT; i++) {
        supervisor_spawn_port(i);
    }
    supervisor_spawn("generator", NULL);
    supervisor_spawn("gateway", gateway_addr);

    /* Stację zatrzymuje sygnał (SIGINT, SIGTERM, SIGHUP) albo komenda operatora shutdown. */
    int running = 1;
    while (running && !signals_stop_requested()) {
        struct msg m;
        if (ipc_mq_receive(ctrl, &m, LOOP_TICK_MS)) {
            running = control_handle(&m);
        }
        supervisor_reap();
        control_tick();
    }

    log_event("zatrzymywanie stacji");
    ipc_lock(mutex);
    state->stopping = 1;
    ipc_unlock(mutex);
    supervisor_stop_all();
    control_close();
    log_event("koniec");
    log_close();

    for (int i = 0; i < PORT_COUNT; i++) {
        ipc_mq_close(ports[i]);
    }
    ipc_mq_close(gen);
    ipc_mq_close(ctrl);
    ipc_sem_close(energy);
    ipc_sem_close(mutex);
    ipc_shm_detach(state);
    ipc_unlink_all();
    return EXIT_SUCCESS;
}

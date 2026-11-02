/* Stałe całego systemu: liczności, nazwy obiektów IPC, czasy, ścieżki.
 * Jedyne miejsce na "magiczne liczby" - reszta kodu używa tych nazw. */
#ifndef CONFIG_H
#define CONFIG_H

#define PORT_COUNT 3
#define ENERGY_UNITS 2
#define CHILD_COUNT (PORT_COUNT + 2) /* porty + generator + gateway */
#define SHIP_MAX 8 /* statków naraz: limit generatora, rekordów sterownika i kolejki zgłoszeń */

/* Nazwy obiektów IPC (widoczne w /dev/shm i /dev/mqueue). */
#define NAME_SHM "/ss_state"
#define NAME_MUTEX "/ss_mutex"
#define NAME_ENERGY "/ss_energy"
#define NAME_CTRL "/ss_ctrl"
#define NAME_GEN "/ss_gen"
#define NAME_PORT_PREFIX "/ss_port"  /* + numer portu 0..PORT_COUNT-1 */
#define NAME_SHIP_PREFIX "/ss_ship_" /* + pid statku */
#define NAME_LEN 32
#define LOCK_PATH "/tmp/ss_station.lock" /* blokada: najwyżej jedna działająca stacja */
#define QUEUE_CAPACITY 10                /* domyślny limit Linuksa dla zwykłego użytkownika */

#define LOOP_TICK_MS 100 /* co tyle pętle główne sprawdzają flagi i liczniki czasu */

/* Czasy etapów pracy portu w ms. */
#define DOCK_MS 3000
#define UNDOCK_MS 2000
#define EQUALIZE_MS 2000
#define LOCK_CLOSE_MS 1000
#define REPAIR_MS 5000
/* Czas obsługi zależy od typu statku. */
#define SERVICE_SHUTTLE_MS 4000
#define SERVICE_FREIGHTER_MS 10000
#define SERVICE_EMERGENCY_MS 6000

#define ACK_TIMEOUT_MS 5000 /* tyle sterownik czeka na potwierdzenie przydziału portu */
#define STARVATION_MS 30000 /* po tylu ms w kolejce statek jest traktowany jak awaryjny */
#define SPAWN_MIN_MS 3000   /* generator: najkrótszy odstęp między statkami */
#define SPAWN_MAX_MS 8000   /* generator: najdłuższy odstęp między statkami */
#define WAVE_SIZE 5         /* tyle statków dokłada polecenie "fala" */

#define BIN_DIR "./build"
#define PATH_LEN 64 /* ścieżka programu: BIN_DIR + "/" + nazwa */
#define LOG_PATH "stacja.log"
#define LOG_LINES 64
#define LOG_LINE_LEN 96

#define GATEWAY_ADDR "127.0.0.1" /* domyślny adres bramy */
#define GATEWAY_PORT 5555
#define PACKET_MAX 256
#define KEY_PATH "config/klucz" /* wspólny klucz HMAC bramy i operatora; tworzy go make */
#define KEY_MIN_LEN 32          /* najkrótszy klucz HMAC w bajtach */
#define KEY_MAX_LEN 128         /* najdłuższy klucz HMAC w bajtach */
/* Znacznik czasu pakietu może różnić się od zegara odbiorcy o mniej niż tyle ms (ochrona przed
 * powtórzeniem przechwyconego pakietu po restarcie bramy). */
#define PACKET_WINDOW_MS 30000
#define OPERATOR_REPLY_MS 1000 /* tyle operator czeka na odpowiedź bramy */

#endif

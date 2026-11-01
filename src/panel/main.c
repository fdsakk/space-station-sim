/* Panel operatora (ncurses): pokazuje stan stacji z pamięci dzielonej i wysyła komendy
 * operatora do sterownika. Wywołany z komendą w argumentach wysyła ją i kończy się. */
#include <fcntl.h>
#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/commands.h"
#include "../common/config.h"
#include "../common/die.h"
#include "../common/ipc.h"
#include "../common/signals.h"
#include "../common/timeutil.h"
#include "../port/port_fsm.h"

#define INPUT_LEN 64      /* najdłuższa komenda wpisywana przez operatora */
#define STATUS_LEN 128    /* wynik ostatniej komendy */
#define KEY_ASCII_DEL 127 /* tak Backspace wysyła wiele terminali */
#define COLOR_FAULT 1     /* numer pary kolorów dla portu w awarii */

/* Kolumny ekranu: tabela portów i lista statków obok kolejki. */
#define COL_STATE 6
#define COL_LOCK 20
#define COL_SERVICE 34
#define COL_ENERGY 52
#define COL_SHIP 60
#define COL_SHIPS 41

/* Kopia stanu stacji, na której panel rysuje ekran poza sekcją krytyczną. */
struct view {
    struct station_state state;
    int energy_free; /* wolne jednostki energii (wartość semafora /ss_energy) */
};

/* Zwraca NULL, gdy komenda poszła do sterownika, a w przeciwnym razie opis błędu.
 * Komendy idą do /ss_ctrl z O_NONBLOCK: pełna kolejka sterownika nie zawiesi panelu. */
static const char *send_command(mqd_t ctrl, const char *line)
{
    struct msg m;
    if (!commands_parse(line, &m)) {
        return "nieznana komenda";
    }
    m.sender = getpid();
    if (!ipc_mq_try_send(ctrl, &m)) {
        return "sterownik nie odbiera (kolejka pełna)";
    }
    return NULL;
}

/* Tryb jednej komendy: wysyła wiersz i kończy się, bez ekranu i pamięci dzielonej. */
static int run_once(const char *line)
{
    mqd_t ctrl = ipc_mq_attach(NAME_CTRL, O_WRONLY | O_NONBLOCK);
    const char *error = send_command(ctrl, line);
    ipc_mq_close(ctrl);
    if (error != NULL) {
        fprintf(stderr, "panel: %s: '%s'\nkomendy: %s\n", error, line, COMMANDS_HELP);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/* Sekcja krytyczna tylko na kopię; rysowanie (wolne, terminal) jest po zwolnieniu mutexu. */
static void take_snapshot(const struct station_state *shared, sem_t *mutex, sem_t *energy,
                          struct view *out)
{
    ipc_lock(mutex);
    out->state = *shared;
    int result = sem_getvalue(energy, &out->energy_free);
    ipc_unlock(mutex);
    if (result == -1) {
        die("sem_getvalue");
    }
}

/* Wypisuje tekst do końca wiersza ekranu i ucina resztę (ncurses zawinąłby ją do następnego
 * wiersza). Limit liczy bajty, więc wiersz z polskimi literami utnie się nieco wcześniej. */
static void print_clipped(int row, int col, const char *text)
{
    mvaddnstr(row, col, text, COLS - col);
}

static void draw_header(const struct view *v)
{
    char energy[ENERGY_UNITS + 1];
    for (int i = 0; i < ENERGY_UNITS; i++) {
        energy[i] = i < v->energy_free ? '#' : '-';
    }
    energy[ENERGY_UNITS] = '\0';
    char line[80];
    snprintf(line, sizeof line, "STACJA KOSMICZNA          energia [%s] wolne %d z %d", energy,
             v->energy_free, ENERGY_UNITS);
    attron(A_BOLD);
    print_clipped(0, 0, line);
    attroff(A_BOLD);
}

static void draw_port(int row, int number, const struct port_record *p)
{
    char text[32];
    snprintf(text, sizeof text, "%d", number);
    print_clipped(row, 1, text);
    int broken = p->state == PORT_BROKEN || p->state == PORT_REPAIRING;
    if (broken) {
        attron(COLOR_PAIR(COLOR_FAULT) | A_BOLD);
    }
    print_clipped(row, COL_STATE, port_state_name(p->state));
    if (broken) {
        attroff(COLOR_PAIR(COLOR_FAULT) | A_BOLD);
    }
    print_clipped(row, COL_LOCK, lock_state_name(p->lock));
    print_clipped(row, COL_SERVICE, service_state_name(p->service));
    print_clipped(row, COL_ENERGY, p->has_energy ? "#" : "");
    if (p->ship != 0) {
        snprintf(text, sizeof text, "%d %s", (int)p->ship, ship_type_name(p->ship_type));
        print_clipped(row, COL_SHIP, text);
    }
}

static void draw_ports(const struct view *v)
{
    attron(A_BOLD);
    print_clipped(2, 0, "PORT");
    print_clipped(2, COL_STATE, "STAN");
    print_clipped(2, COL_LOCK, "ŚLUZA");
    print_clipped(2, COL_SERVICE, "OBSŁUGA");
    print_clipped(2, COL_ENERGY, "ENERGIA");
    print_clipped(2, COL_SHIP, "STATEK");
    attroff(A_BOLD);
    for (int i = 0; i < PORT_COUNT; i++) {
        draw_port(3 + i, i + 1, &v->state.ports[i]); /* numer dla ludzi */
    }
}

/* Kolejka (po lewej) i rekordy statków (po prawej). Zwraca pierwszy wolny wiersz pod nimi. */
static int draw_queue_and_ships(const struct view *v, int row)
{
    char text[64];
    snprintf(text, sizeof text, "KOLEJKA (%d), czas czekania", v->state.queue.count);
    print_clipped(row, 0, text);
    print_clipped(row, COL_SHIPS, "STATKI");
    long long now = time_now_ms();
    for (int i = 0; i < v->state.queue.count; i++) {
        const struct queue_entry *e = &v->state.queue.items[i];
        snprintf(text, sizeof text, " %d %s %lld s", (int)e->pid, ship_type_name(e->type),
                 (now - e->since_ms) / 1000);
        print_clipped(row + 1 + i, 0, text);
    }
    int shown = 0;
    for (int i = 0; i < SHIP_MAX; i++) {
        const struct ship_record *s = &v->state.ships[i];
        if (s->pid == 0) {
            continue;
        }
        snprintf(text, sizeof text, "%d %s %s", (int)s->pid, ship_type_name(s->type),
                 ship_state_name(s->state));
        if (s->port != -1) {
            size_t used = strlen(text);
            snprintf(text + used, sizeof text - used, " p%d", s->port + 1);
        }
        print_clipped(row + 1 + shown, COL_SHIPS, text);
        shown++;
    }
    int rows = shown > v->state.queue.count ? shown : v->state.queue.count;
    return row + 2 + rows;
}

/* Najnowsze wpisy dziennika, które mieszczą się w wierszach first..last. */
static void draw_log(const struct log_ring *log, int first, int last)
{
    print_clipped(first, 0, "DZIENNIK");
    int rows = last - first;
    int shown = log->count < rows ? log->count : rows;
    for (int i = 0; i < shown; i++) {
        int index = (log->next - shown + i + LOG_LINES) % LOG_LINES;
        print_clipped(first + 1 + i, 0, log->lines[index]);
    }
}

static void draw(const struct view *v, const char *input, const char *status)
{
    erase();
    draw_header(v);
    draw_ports(v);
    int log_row = draw_queue_and_ships(v, 3 + PORT_COUNT + 1);
    draw_log(&v->state.log, log_row, LINES - 4);
    print_clipped(LINES - 3, 0, "komendy: " COMMANDS_HELP);
    print_clipped(LINES - 2, 0,
                  "typy statków: wahadlowiec, transportowiec, awaryjny; Ctrl+C: "
                  "zamknij panel");
    char line[INPUT_LEN + 2];
    snprintf(line, sizeof line, "> %s", input);
    print_clipped(LINES - 1, 0, line);
    print_clipped(LINES - 1, COLS / 2, status);
    move(LINES - 1, 2 + (int)strlen(input));
    refresh();
}

/* Wiersz komendy: znaki dopisujemy, Backspace kasuje, Enter wysyła i zapisuje wynik w status. */
static void handle_key(int key, char *input, mqd_t ctrl, char status[STATUS_LEN])
{
    size_t len = strlen(input);
    if (key == '\n' || key == KEY_ENTER) {
        const char *error = send_command(ctrl, input);
        snprintf(status, STATUS_LEN, "[%s: %s]", error != NULL ? error : "wysłano", input);
        input[0] = '\0';
    } else if ((key == KEY_BACKSPACE || key == KEY_ASCII_DEL || key == '\b') && len > 0) {
        input[len - 1] = '\0';
    } else if (key >= ' ' && key <= '~' && len < INPUT_LEN - 1) {
        input[len] = (char)key;
        input[len + 1] = '\0';
    }
}

static void screen_start(void)
{
    setlocale(LC_ALL, ""); /* polskie znaki w UTF-8 */
    initscr();
    cbreak(); /* Ctrl+C dalej wysyła SIGINT, który kończy panel */
    noecho();
    keypad(stdscr, TRUE);
    /* getch czeka na klawisz najwyżej LOOP_TICK_MS: co tyle odświeżamy ekran (bez aktywnego
     * czekania). */
    timeout(LOOP_TICK_MS);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(COLOR_FAULT, COLOR_RED, -1);
    }
}

static int run_screen(void)
{
    struct station_state *shared = ipc_shm_attach();
    sem_t *mutex = ipc_sem_attach(NAME_MUTEX);
    sem_t *energy = ipc_sem_attach(NAME_ENERGY);
    mqd_t ctrl = ipc_mq_attach(NAME_CTRL, O_WRONLY | O_NONBLOCK);
    screen_start();

    struct view view = {0};
    char input[INPUT_LEN] = "";
    char status[STATUS_LEN] = "";
    while (!signals_stop_requested()) {
        take_snapshot(shared, mutex, energy, &view);
        if (view.state.stopping) {
            break;
        }
        draw(&view, input, status);
        handle_key(getch(), input, ctrl, status);
    }

    endwin();
    if (view.state.stopping) {
        puts("panel: stacja zatrzymana");
    }
    ipc_mq_close(ctrl);
    ipc_sem_close(energy);
    ipc_sem_close(mutex);
    ipc_shm_detach(shared);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        /* Słowa z argumentów programu tworzą wiersz komendy, np. "awaria 2". */
        char line[INPUT_LEN] = "";
        for (int i = 1; i < argc; i++) {
            size_t used = strlen(line);
            snprintf(line + used, sizeof line - used, "%s%s", i > 1 ? " " : "", argv[i]);
        }
        return run_once(line);
    }
    signals_setup();
    return run_screen();
}

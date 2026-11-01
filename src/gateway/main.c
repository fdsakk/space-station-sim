/* Brama sieciowa: odbiera pakiety UDP od zdalnego operatora, sprawdza podpis HMAC i znacznik
 * czasu, poprawne komendy przekazuje sterownikowi i odsyła odpowiedź. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../common/commands.h"
#include "../common/config.h"
#include "../common/die.h"
#include "../common/hmac.h"
#include "../common/ipc.h"
#include "../common/log.h"
#include "../common/signals.h"
#include "../common/timeutil.h"

struct gateway {
    int sock;
    mqd_t ctrl;
    struct hmac_key key;
    long long
        last_stamp_ms; /* znacznik ostatniego przyjętego pakietu (ochrona przed powtórzeniem) */
};

static int open_socket(const char *address)
{
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(GATEWAY_PORT);
    if (inet_pton(AF_INET, address, &addr.sin_addr) != 1) {
        fprintf(stderr, "gateway: niepoprawny adres IPv4 '%s'\n", address);
        exit(EXIT_FAILURE);
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        die("socket");
    }
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) == -1) {
        die("bind");
    }
    /* Limit czasu odbioru: recvfrom wraca co LOOP_TICK_MS, więc pętla widzi flagę zatrzymania. */
    struct timeval tick = {0, LOOP_TICK_MS * 1000};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tick, sizeof tick) == -1) {
        die("setsockopt");
    }
    return sock;
}

/* Odpowiedź to zwykły tekst dla operatora; podpisujemy tylko komendy, bo tylko one coś zmieniają.
 */
static void send_reply(const struct gateway *g, const struct sockaddr_in *to, const char *result)
{
    if (sendto(g->sock, result, strlen(result), 0, (const struct sockaddr *)to, sizeof *to) == -1) {
        /* Błąd w trakcie pracy (np. brak trasy do nadawcy): wpis w dzienniku, brama działa dalej.
         */
        log_event("nie wysłano odpowiedzi: %s", strerror(errno));
    }
}

/* Komenda z pakietu, który przeszedł wszystkie sprawdzenia -> OP_* do sterownika.
 * Zwraca wynik dla operatora. */
static const char *execute(const struct gateway *g, const char *source, const char *text)
{
    struct msg m;
    if (!commands_parse(text, &m)) {
        log_event("nieznana komenda z %s: %s", source, text);
        return "nieznana komenda";
    }
    m.sender = getpid();
    /* O_NONBLOCK jak w panelu: pełna kolejka sterownika nie zawiesza bramy. */
    if (!ipc_mq_try_send(g->ctrl, &m)) {
        log_event("komenda z %s nie wysłana, kolejka sterownika pełna", source);
        return "sterownik nie odbiera";
    }
    log_event("komenda z %s: %s", source, text);
    return "ok";
}

/* Kolejność sprawdzeń: długość, format i podpis, znacznik czasu, komenda.
 * Treść pakietu trafia do dziennika dopiero po sprawdzeniu podpisu. */
static void handle_packet(struct gateway *g, char *packet, ssize_t len,
                          const struct sockaddr_in *from)
{
    char source[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &from->sin_addr, source, sizeof source) == NULL) {
        die("inet_ntop");
    }
    /* Bufor ma PACKET_MAX + 1 bajtów: dłuższy pakiet system ucina, ale wtedy len > PACKET_MAX. */
    if (len > PACKET_MAX) {
        log_event("odrzucono pakiet z %s: za długi", source);
        return;
    }
    packet[len] = '\0';
    long long stamp_ms = 0;
    char text[PACKET_MAX + 1];
    enum packet_status status = hmac_packet_open(&g->key, packet, &stamp_ms, text, sizeof text);
    if (status == PACKET_OK) {
        status = hmac_check_time(stamp_ms, time_wall_ms(), g->last_stamp_ms);
    }
    /* Na pakiety bez poprawnego podpisu nie odpowiadamy: nadawca bez klucza nie dostaje informacji
     * zwrotnej, a brama nie odsyła ruchu na podrobiony adres nadawcy. */
    switch (status) {
    case PACKET_OK:
        g->last_stamp_ms = stamp_ms;
        send_reply(g, from, execute(g, source, text));
        break;
    case PACKET_BAD_FORMAT:
        log_event("odrzucono pakiet z %s: zły format", source);
        break;
    case PACKET_BAD_HMAC:
        log_event("odrzucono pakiet z %s: zły podpis", source);
        break;
    case PACKET_OUT_OF_WINDOW:
        log_event("odrzucono pakiet z %s: znacznik czasu poza oknem", source);
        send_reply(g, from, "znacznik czasu poza oknem");
        break;
    case PACKET_REPLAYED:
        log_event("odrzucono pakiet z %s: powtórzony", source);
        send_reply(g, from, "pakiet powtorzony");
        break;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "użycie: gateway <adres>\n");
        return EXIT_FAILURE;
    }
    signals_setup();
    struct gateway g = {0};
    hmac_load_key(KEY_PATH, &g.key);
    struct station_state *state = ipc_shm_attach();
    sem_t *mutex = ipc_sem_attach(NAME_MUTEX);
    log_init("gateway", state, mutex);
    g.ctrl = ipc_mq_attach(NAME_CTRL, O_WRONLY | O_NONBLOCK);
    g.sock = open_socket(argv[1]);
    log_event("start, UDP %s:%d", argv[1], GATEWAY_PORT);

    while (!signals_stop_requested()) {
        char packet[PACKET_MAX + 1];
        struct sockaddr_in from;
        socklen_t from_len = sizeof from;
        ssize_t len =
            recvfrom(g.sock, packet, sizeof packet, 0, (struct sockaddr *)&from, &from_len);
        if (len >= 0) {
            handle_packet(&g, packet, len, &from);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            /* Minął czas (EAGAIN) albo przerwał sygnał (EINTR) - to normalne, pętla sprawdzi flagę.
             * Każdy inny błąd powtarzałby się natychmiast, czyli aktywne czekanie. */
            die("recvfrom");
        }
    }

    log_event("koniec");
    log_close();
    if (close(g.sock) == -1) {
        die("close");
    }
    ipc_mq_close(g.ctrl);
    ipc_sem_close(mutex);
    ipc_shm_detach(state);
    return EXIT_SUCCESS;
}

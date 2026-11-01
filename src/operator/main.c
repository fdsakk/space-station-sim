/* Zdalny operator: czyta komendy z klawiatury, wysyła je do bramy stacji jako podpisane pakiety
 * UDP i wypisuje odpowiedź. Działa poza drzewem procesów stacji. */
#include <arpa/inet.h>
#include <errno.h>
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
#include "../common/timeutil.h"

#define LINE_LEN 128 /* najdłuższy wiersz z klawiatury */

/* Gniazdo "połączone" z bramą: send() wysyła do niej, a recv() przyjmuje tylko od niej.
 * Limit czasu odbioru: UDP nie gwarantuje odpowiedzi, a recv nie może czekać bez końca. */
static int open_socket(const char *address)
{
    struct sockaddr_in gateway = {0};
    gateway.sin_family = AF_INET;
    gateway.sin_port = htons(GATEWAY_PORT);
    if (inet_pton(AF_INET, address, &gateway.sin_addr) != 1) {
        fprintf(stderr, "operator: niepoprawny adres IPv4 '%s'\n", address);
        exit(EXIT_FAILURE);
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        die("socket");
    }
    if (connect(sock, (struct sockaddr *)&gateway, sizeof gateway) == -1) {
        die("connect");
    }
    struct timeval limit = {OPERATOR_REPLY_MS / 1000, OPERATOR_REPLY_MS % 1000 * 1000};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit) == -1) {
        die("setsockopt");
    }
    return sock;
}

/* Wypisuje odpowiedź bramy albo informację, że nie przyszła. ECONNREFUSED oznacza, że nikt nie
 * nasłuchuje pod adresem bramy (stacja nie działa). */
static void print_reply(int sock)
{
    char reply[PACKET_MAX + 1];
    ssize_t n = recv(sock, reply, PACKET_MAX, 0);
    if (n >= 0) {
        reply[n] = '\0';
        printf("stacja: %s\n", reply);
    } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) {
        printf("brak odpowiedzi stacji w %d ms\n", OPERATOR_REPLY_MS);
    } else {
        die("recv");
    }
}

/* last_stamp_ms: znacznik poprzedniej komendy. Brama przyjmuje tylko znaczniki większe od
 * ostatniego, a komendy z potoku (echo ... | operator) mogą wyjść w tej samej milisekundzie. */
static void run_command(int sock, const struct hmac_key *key, long long *last_stamp_ms,
                        const char *line)
{
    long long stamp_ms = time_wall_ms();
    if (stamp_ms <= *last_stamp_ms) {
        stamp_ms = *last_stamp_ms + 1;
    }
    *last_stamp_ms = stamp_ms;
    char packet[PACKET_MAX + 1];
    if (!hmac_packet_build(key, stamp_ms, line, packet, sizeof packet)) {
        printf("komenda za długa\n");
        return;
    }
    if (send(sock, packet, strlen(packet), 0) == -1) {
        perror("send");
        return;
    }
    print_reply(sock);
}

int main(int argc, char *argv[])
{
    if (argc > 2) {
        fprintf(stderr, "użycie: operator [adres stacji, domyślnie %s]\n", GATEWAY_ADDR);
        return EXIT_FAILURE;
    }
    struct hmac_key key;
    hmac_load_key(KEY_PATH, &key);
    int sock = open_socket(argc == 2 ? argv[1] : GATEWAY_ADDR);
    printf("komendy: %s (koniec: Ctrl+D)\n", COMMANDS_HELP);

    long long last_stamp_ms = 0;
    char line[LINE_LEN];
    while (fgets(line, sizeof line, stdin) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] != '\0') {
            run_command(sock, &key, &last_stamp_ms, line);
        }
    }

    if (close(sock) == -1) {
        die("close");
    }
    return EXIT_SUCCESS;
}

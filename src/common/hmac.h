/* Pakiety sieciowe operatora: "<znacznik_czasu_ms> <tekst> <hmac_hex>" podpisane HMAC-SHA256
 * wspólnym kluczem. Poza wczytaniem klucza czyste funkcje: czas przychodzi
 * w argumentach. */
#ifndef HMAC_H
#define HMAC_H

#include <stddef.h>

#include "config.h"

#define HMAC_HEX_LEN 64 /* SHA-256 to 32 bajty, czyli 64 znaki szesnastkowe */

struct hmac_key {
    char text[KEY_MAX_LEN + 1];
    size_t len;
};

enum packet_status {
    PACKET_OK,
    PACKET_BAD_FORMAT,    /* nie pasuje do "<znacznik> <tekst> <hmac>" */
    PACKET_BAD_HMAC,      /* podpis nie zgadza się z treścią */
    PACKET_OUT_OF_WINDOW, /* znacznik czasu za daleko od zegara odbiorcy */
    PACKET_REPLAYED       /* znacznik nie większy od ostatnio przyjętego */
};

/* Wczytuje klucz z pierwszego wiersza pliku (przy starcie; błąd kończy proces). */
void hmac_load_key(const char *path, struct hmac_key *key);
/* Składa podpisany pakiet w buf. Zwraca 0, gdy pakiet nie mieści się w buf. */
int hmac_packet_build(const struct hmac_key *key, long long stamp_ms, const char *text, char *buf,
                      size_t len);
/* Sprawdza format i podpis. Przy PACKET_OK wpisuje znacznik i tekst (text ma len bajtów). */
enum packet_status hmac_packet_open(const struct hmac_key *key, const char *packet,
                                    long long *stamp_ms, char *text, size_t len);
/* Sprawdza znacznik podpisanego pakietu: okno PACKET_WINDOW_MS wokół now_ms i powtórzenie. */
enum packet_status hmac_check_time(long long stamp_ms, long long now_ms, long long last_ms);

#endif

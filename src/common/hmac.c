/* Pakiety sieciowe operatora: składanie, sprawdzenie formatu i podpisu HMAC-SHA256, okno czasowe
 * i ochrona przed powtórzeniem. */
#include "hmac.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "die.h"

/* Podpis HMAC-SHA256 danych jako 64 małe cyfry szesnastkowe (out ma HMAC_HEX_LEN + 1 bajtów). */
static void sign_hex(const struct hmac_key *key, const char *data, size_t len, char *out)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    if (HMAC(EVP_sha256(), key->text, (int)key->len, (const unsigned char *)data, len, digest,
             &digest_len) == NULL) {
        die("HMAC");
    }
    for (unsigned int i = 0; i < digest_len; i++) {
        snprintf(out + 2 * i, 3, "%02x", digest[i]);
    }
}

void hmac_load_key(const char *path, struct hmac_key *key)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        die(path);
    }
    /* Miejsce na znak końca wiersza i '\0': dłuższy wiersz da długość ponad KEY_MAX_LEN. */
    char line[KEY_MAX_LEN + 2];
    int has_line = fgets(line, sizeof line, file) != NULL;
    if (fclose(file) == EOF) {
        die(path);
    }
    size_t len = has_line ? strcspn(line, "\n") : 0;
    if (len < KEY_MIN_LEN || len > KEY_MAX_LEN) {
        fprintf(stderr, "%s: klucz musi mieć od %d do %d znaków\n", path, KEY_MIN_LEN, KEY_MAX_LEN);
        exit(EXIT_FAILURE);
    }
    memcpy(key->text, line, len);
    key->text[len] = '\0';
    key->len = len;
}

int hmac_packet_build(const struct hmac_key *key, long long stamp_ms, const char *text, char *buf,
                      size_t len)
{
    int signed_len = snprintf(buf, len, "%lld %s", stamp_ms, text);
    /* Za podpisaną częścią musi się zmieścić spacja, podpis i '\0'. */
    if (signed_len < 0 || (size_t)signed_len + 1 + HMAC_HEX_LEN + 1 > len) {
        return 0;
    }
    buf[signed_len] = ' ';
    sign_hex(key, buf, (size_t)signed_len, buf + signed_len + 1);
    return 1;
}

enum packet_status hmac_packet_open(const struct hmac_key *key, const char *packet,
                                    long long *stamp_ms, char *text, size_t len)
{
    /* Najpierw podział na części: "<znacznik> <tekst> <hmac>". Tekst może mieć spacje, więc
     * znacznik kończy pierwsza spacja, a podpis zaczyna ostatnia. Treści nie sprawdzamy osobno,
     * bo poprawny podpis potwierdza, że pakiet pochodzi od operatora. */
    const char *first = strchr(packet, ' ');
    const char *last = strrchr(packet, ' ');
    if (first == NULL || last - first < 2) {
        return PACKET_BAD_FORMAT;
    }
    size_t text_len = (size_t)(last - first - 1);
    const char *mac = last + 1;
    if (strlen(mac) != HMAC_HEX_LEN || text_len + 1 > len) {
        return PACKET_BAD_FORMAT;
    }

    /* CRYPTO_memcmp porównuje w stałym czasie: czas odpowiedzi nie zdradza, ile pierwszych znaków
     * podpisu atakujący zgadł. */
    char expected[HMAC_HEX_LEN + 1];
    sign_hex(key, packet, (size_t)(last - packet), expected);
    if (CRYPTO_memcmp(expected, mac, HMAC_HEX_LEN) != 0) {
        return PACKET_BAD_HMAC;
    }
    *stamp_ms = strtoll(packet, NULL, 10);
    memcpy(text, first + 1, text_len);
    text[text_len] = '\0';
    return PACKET_OK;
}

enum packet_status hmac_check_time(long long stamp_ms, long long now_ms, long long last_ms)
{
    /* Okno chroni po restarcie bramy (last_ms wraca do 0), a last_ms w obrębie okna. */
    if (llabs(now_ms - stamp_ms) >= PACKET_WINDOW_MS) {
        return PACKET_OUT_OF_WINDOW;
    }
    if (stamp_ms <= last_ms) {
        return PACKET_REPLAYED;
    }
    return PACKET_OK;
}

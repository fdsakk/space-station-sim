/* Testy podpisu pakietów sieciowych: zgodność z OpenSSL, odrzucanie zmienionych i błędnych
 * pakietów, okno czasowe i powtórzenie. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/common/hmac.h"

#define KEY "klucz-testowy-do-testow-jednostkowych"
#define STAMP 1792339200123LL
/* Podpis policzony niezależnie od naszego kodu, programem openssl z kluczem KEY:
 * printf '1792339200123 awaria 2' | openssl dgst -sha256 -hmac <KEY> */
#define EXPECTED                                                                                   \
    "1792339200123 awaria 2 f51880d50e15516ef80b36a878acbb1cdcb51a2b80cfd287ed09b495e8fce027"

static struct hmac_key make_key(const char *text)
{
    struct hmac_key key;
    snprintf(key.text, sizeof key.text, "%s", text);
    key.len = strlen(key.text);
    return key;
}

static enum packet_status open_packet(const char *packet)
{
    struct hmac_key key = make_key(KEY);
    long long stamp;
    char text[PACKET_MAX + 1];
    return hmac_packet_open(&key, packet, &stamp, text, sizeof text);
}

static void test_build_matches_openssl(void)
{
    struct hmac_key key = make_key(KEY);
    char packet[PACKET_MAX + 1];
    assert(hmac_packet_build(&key, STAMP, "awaria 2", packet, sizeof packet));
    assert(strcmp(packet, EXPECTED) == 0);
}

static void test_open_returns_stamp_and_text(void)
{
    struct hmac_key key = make_key(KEY);
    long long stamp;
    char text[PACKET_MAX + 1];
    assert(hmac_packet_open(&key, EXPECTED, &stamp, text, sizeof text) == PACKET_OK);
    assert(stamp == STAMP);
    assert(strcmp(text, "awaria 2") == 0);
}

static void test_build_rejects_too_small_buffer(void)
{
    struct hmac_key key = make_key(KEY);
    char small[HMAC_HEX_LEN];
    assert(!hmac_packet_build(&key, STAMP, "fala", small, sizeof small));
}

static void test_changed_packet_is_rejected(void)
{
    /* Podmieniona komenda, znacznik albo podpis. */
    assert(open_packet("1792339200123 awaria 3 "
                       "f51880d50e15516ef80b36a878acbb1cdcb51a2b80cfd287ed09b495e8fce027") ==
           PACKET_BAD_HMAC);
    assert(open_packet("1792339200124 awaria 2 "
                       "f51880d50e15516ef80b36a878acbb1cdcb51a2b80cfd287ed09b495e8fce027") ==
           PACKET_BAD_HMAC);
    assert(open_packet("1792339200123 awaria 2 "
                       "051880d50e15516ef80b36a878acbb1cdcb51a2b80cfd287ed09b495e8fce027") ==
           PACKET_BAD_HMAC);
    /* Inny klucz. */
    struct hmac_key other = make_key("inny-klucz-inny-klucz-inny-klucz-inny");
    long long stamp;
    char text[PACKET_MAX + 1];
    assert(hmac_packet_open(&other, EXPECTED, &stamp, text, sizeof text) == PACKET_BAD_HMAC);
}

static void test_bad_format_is_rejected(void)
{
    const char *mac = "f51880d50e15516ef80b36a878acbb1cdcb51a2b80cfd287ed09b495e8fce027";
    char packet[PACKET_MAX + 1];
    assert(open_packet("") == PACKET_BAD_FORMAT);
    assert(open_packet("awaria") == PACKET_BAD_FORMAT);
    snprintf(packet, sizeof packet, "1792339200123 %s", mac); /* brak tekstu */
    assert(open_packet(packet) == PACKET_BAD_FORMAT);
    assert(open_packet("1792339200123 awaria 2 f51880d5") == PACKET_BAD_FORMAT);
    /* Zmieniona wielkość liter to już inny podpis. */
    assert(open_packet("1792339200123 awaria 2 "
                       "F51880D50E15516EF80B36A878ACBB1CDCB51A2B80CFD287ED09B495E8FCE027") ==
           PACKET_BAD_HMAC);
}

static void test_text_must_fit(void)
{
    struct hmac_key key = make_key(KEY);
    long long stamp;
    char text[4];
    assert(hmac_packet_open(&key, EXPECTED, &stamp, text, sizeof text) == PACKET_BAD_FORMAT);
}

static void test_time_window(void)
{
    long long now = STAMP;
    assert(hmac_check_time(now, now, 0) == PACKET_OK);
    assert(hmac_check_time(now - PACKET_WINDOW_MS + 1, now, 0) == PACKET_OK);
    assert(hmac_check_time(now + PACKET_WINDOW_MS - 1, now, 0) == PACKET_OK);
    assert(hmac_check_time(now - PACKET_WINDOW_MS, now, 0) == PACKET_OUT_OF_WINDOW);
    assert(hmac_check_time(now + PACKET_WINDOW_MS, now, 0) == PACKET_OUT_OF_WINDOW);
}

static void test_replay(void)
{
    long long now = STAMP;
    assert(hmac_check_time(now - 10, now, now - 10) == PACKET_REPLAYED);
    assert(hmac_check_time(now - 20, now, now - 10) == PACKET_REPLAYED);
    assert(hmac_check_time(now - 9, now, now - 10) == PACKET_OK);
}

int main(void)
{
    test_build_matches_openssl();
    test_open_returns_stamp_and_text();
    test_build_rejects_too_small_buffer();
    test_changed_packet_is_rejected();
    test_bad_format_is_rejected();
    test_text_must_fit();
    test_time_window();
    test_replay();
    printf("test_hmac: OK\n");
    return 0;
}

/* Testy parsera komend operatora: poprawne komendy i odrzucanie błędnych. */
#include <assert.h>
#include <stdio.h>

#include "../../src/common/commands.h"

static struct msg parse_ok(const char *line)
{
    struct msg m;
    assert(commands_parse(line, &m));
    return m;
}

static void test_port_commands(void)
{
    struct msg m = parse_ok("awaria 2");
    assert(m.type == OP_FAULT && m.port == 1); /* numer dla ludzi 1..3, indeks 0..2 */
    m = parse_ok("napraw 1");
    assert(m.type == OP_REPAIR && m.port == 0);
    m = parse_ok("napraw 3");
    assert(m.type == OP_REPAIR && m.port == 2);
}

static void test_ship_commands(void)
{
    struct msg m = parse_ok("statek awaryjny");
    assert(m.type == OP_SPAWN && m.ship_type == SHIP_TYPE_EMERGENCY);
    m = parse_ok("statek transportowiec");
    assert(m.type == OP_SPAWN && m.ship_type == SHIP_TYPE_FREIGHTER);
    assert(parse_ok("fala").type == OP_WAVE);
    assert(parse_ok("shutdown").type == OP_SHUTDOWN);
}

static void test_whitespace_is_ignored(void)
{
    struct msg m = parse_ok("  awaria   3 \n");
    assert(m.type == OP_FAULT && m.port == 2);
    assert(parse_ok("fala\n").type == OP_WAVE);
}

static void test_rejects_bad_commands(void)
{
    const char *bad[] = {
        "",
        "   ",
        "lot",
        "awaria",
        "awaria 0",
        "awaria 4",
        "awaria -1",
        "awaria 2x",
        "awaria dwa",
        "awaria 1 2",
        "napraw",
        "statek",
        "statek rakieta",
        "statek awaryjny 2",
        "fala 2",
        "shutdown x",
        "AWARIA 1",
        "awaria 99999999999999999999",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        struct msg m;
        assert(!commands_parse(bad[i], &m));
    }
}

int main(void)
{
    test_port_commands();
    test_ship_commands();
    test_whitespace_is_ignored();
    test_rejects_bad_commands();
    puts("test_commands: OK");
    return 0;
}

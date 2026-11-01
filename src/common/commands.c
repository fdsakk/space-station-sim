/* Komendy operatora: wiersz tekstu -> komunikat OP_* dla sterownika. */
#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#define WORD_LEN 16 /* najdłuższe słowo komendy to "transportowiec" (14 znaków) */

/* Numer portu dla ludzi (1..PORT_COUNT) -> indeks; -1, gdy napis nie jest takim numerem. */
static int parse_port(const char *text)
{
    char *end;
    long number = strtol(text, &end, 10);
    if (*end != '\0' || number < 1 || number > PORT_COUNT) {
        return -1;
    }
    return (int)number - 1;
}

static int parse_one_word(const char *word, struct msg *out)
{
    if (strcmp(word, "fala") == 0) {
        out->type = OP_WAVE;
        return 1;
    }
    if (strcmp(word, "shutdown") == 0) {
        out->type = OP_SHUTDOWN;
        return 1;
    }
    return 0;
}

static int parse_two_words(const char *word, const char *arg, struct msg *out)
{
    int port = parse_port(arg);
    enum ship_type type;
    if (strcmp(word, "awaria") == 0 && port != -1) {
        out->type = OP_FAULT;
        out->port = port;
        return 1;
    }
    if (strcmp(word, "napraw") == 0 && port != -1) {
        out->type = OP_REPAIR;
        out->port = port;
        return 1;
    }
    if (strcmp(word, "statek") == 0 && ship_type_parse(arg, &type)) {
        out->type = OP_SPAWN;
        out->ship_type = type;
        return 1;
    }
    return 0;
}

int commands_parse(const char *line, struct msg *out)
{
    memset(out, 0, sizeof *out);
    /* Szerokość 15 = WORD_LEN - 1 (miejsce na '\0'). Trzecie pole łapie nadmiarowe słowa:
     * gdy sscanf je wypełni, komenda ma za dużo argumentów. Za długie słowo zostaje ucięte,
     * więc nie pasuje do żadnej komendy. */
    char word[WORD_LEN], arg[WORD_LEN], extra;
    int fields = sscanf(line, "%15s %15s %c", word, arg, &extra);
    if (fields == 1) {
        return parse_one_word(word, out);
    }
    if (fields == 2) {
        return parse_two_words(word, arg, out);
    }
    return 0;
}

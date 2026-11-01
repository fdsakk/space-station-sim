/* Protokół komunikatów: nazwy typów komunikatów i typów statków. */
#include "protocol.h"

#include <string.h>

const char *msg_type_name(enum msg_type type)
{
    switch (type) {
    case SHIP_HELLO:
        return "SHIP_HELLO";
    case SHIP_ACK_ASSIGN:
        return "SHIP_ACK_ASSIGN";
    case SHIP_UNDOCK_REQ:
        return "SHIP_UNDOCK_REQ";
    case ASSIGNED:
        return "ASSIGNED";
    case DOCKED:
        return "DOCKED";
    case SERVICE_DONE:
        return "SERVICE_DONE";
    case DEPART_OK:
        return "DEPART_OK";
    case PORT_FAULT:
        return "PORT_FAULT";
    case CMD_DOCK:
        return "CMD_DOCK";
    case CMD_UNDOCK:
        return "CMD_UNDOCK";
    case CMD_FAULT:
        return "CMD_FAULT";
    case CMD_REPAIR:
        return "CMD_REPAIR";
    case CMD_CANCEL:
        return "CMD_CANCEL";
    case EV_DOCKED:
        return "EV_DOCKED";
    case EV_SERVICE_DONE:
        return "EV_SERVICE_DONE";
    case EV_UNDOCKED:
        return "EV_UNDOCKED";
    case EV_FAULT:
        return "EV_FAULT";
    case CMD_SPAWN:
        return "CMD_SPAWN";
    case CMD_WAVE:
        return "CMD_WAVE";
    case EV_SHIP_LOST:
        return "EV_SHIP_LOST";
    case OP_FAULT:
        return "OP_FAULT";
    case OP_REPAIR:
        return "OP_REPAIR";
    case OP_SPAWN:
        return "OP_SPAWN";
    case OP_WAVE:
        return "OP_WAVE";
    case OP_SHUTDOWN:
        return "OP_SHUTDOWN";
    }
    /* Wartość spoza enum - np. uszkodzony komunikat. */
    return "NIEZNANY";
}

const char *ship_type_name(enum ship_type type)
{
    switch (type) {
    case SHIP_TYPE_SHUTTLE:
        return "wahadlowiec";
    case SHIP_TYPE_FREIGHTER:
        return "transportowiec";
    case SHIP_TYPE_EMERGENCY:
        return "awaryjny";
    }
    return "NIEZNANY";
}

int ship_type_parse(const char *name, enum ship_type *out)
{
    for (int i = 0; i < SHIP_TYPE_COUNT; i++) {
        if (strcmp(name, ship_type_name((enum ship_type)i)) == 0) {
            *out = (enum ship_type)i;
            return 1;
        }
    }
    return 0;
}

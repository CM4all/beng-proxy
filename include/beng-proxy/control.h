/*
 * Definitions for the beng-proxy remote control protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_H
#define BENG_PROXY_CONTROL_H

#include <stdint.h>

enum beng_control_command {
    BENG_CONTROL_NOP,
};

struct beng_control_header {
    uint16_t length;
    uint16_t command;
};

#endif

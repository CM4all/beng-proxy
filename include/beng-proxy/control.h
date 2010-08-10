/*
 * Definitions for the beng-proxy remote control protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_H
#define BENG_PROXY_CONTROL_H

#include <stdint.h>

enum beng_control_command {
    CONTROL_NOP = 0,

    /**
     * Drop items from the translation cache.
     */
    CONTROL_TCACHE_INVALIDATE = 1,
};

struct beng_control_header {
    uint16_t length;
    uint16_t command;
};

/**
 * This magic number precedes every UDP packet.
 */
static const uint32_t control_magic = 0x63046101;

#endif

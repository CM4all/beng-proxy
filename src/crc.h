/*
 * Verify URI parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CRC_H
#define BENG_PROXY_CRC_H

#include <inline/compiler.h>

#include <assert.h>

gcc_const
static inline uint16_t
crc16_update(uint16_t crc, uint8_t data)
{
    crc = crc ^ ((uint16_t)data << 8);
    for (unsigned i = 0; i < 8; ++i) {
        if (crc & 0x8000)
            crc = (crc << 1) ^ 0x1021;
        else
            crc <<= 1;
    }

    return crc;
}

gcc_pure
static inline uint16_t
crc16_string(uint16_t crc, const char *p)
{
    assert(p != NULL);

    while (*p != 0)
        crc = crc16_update(crc, *p++);

    return crc;
}

#endif

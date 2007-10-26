/*
 * Definitions for the beng-proxy translation protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROXY_TRANSLATION_H
#define __BENG_PROXY_TRANSLATION_H

#include <stdint.h>

enum beng_translation_command {
    TRANSLATE_END = 1,
    TRANSLATE_BEGIN = 2,
    TRANSLATE_HOST = 3,
    TRANSLATE_URI = 4,
    TRANSLATE_STATUS = 5,
    TRANSLATE_PATH = 6,
    TRANSLATE_CONTENT_TYPE = 7,
    TRANSLATE_PROXY = 8,
    TRANSLATE_REDIRECT = 9,
    TRANSLATE_FILTER = 10,
    TRANSLATE_PROCESS = 11,
    TRANSLATE_SESSION = 12,
};

struct beng_translation_header {
    uint16_t length;
    uint16_t command;
} __attribute__((packed));

#endif

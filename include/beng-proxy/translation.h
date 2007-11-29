/*
 * Definitions for the beng-proxy translation protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROXY_TRANSLATION_H
#define __BENG_PROXY_TRANSLATION_H

#include <stdint.h>

enum beng_translation_command {
    TRANSLATE_BEGIN = 1,
    TRANSLATE_END = 2,
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
    TRANSLATE_PARAM = 13,
    TRANSLATE_USER = 14,
    TRANSLATE_LANGUAGE = 15,
    TRANSLATE_REMOTE_HOST = 16,
    TRANSLATE_PATH_INFO = 17,
};

struct beng_translation_header {
    uint16_t length;
    uint16_t command;
} __attribute__((packed));

#endif

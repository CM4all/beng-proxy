/*
 * Definitions for the beng-proxy translation protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_PROXY_TRANSLATION_H
#define __BENG_PROXY_TRANSLATION_H

#include <inline/compiler.h>

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
    TRANSLATE_SITE = 18,
    TRANSLATE_CGI = 19,
    TRANSLATE_DOCUMENT_ROOT = 20,
    TRANSLATE_WIDGET_TYPE = 21,
    TRANSLATE_CONTAINER = 22,
    TRANSLATE_ADDRESS = 23,
    TRANSLATE_ADDRESS_STRING = 24,
    TRANSLATE_JAILCGI = 26,
    TRANSLATE_INTERPRETER = 27,
    TRANSLATE_ACTION = 28,
    TRANSLATE_SCRIPT_NAME = 29,
    TRANSLATE_AJP = 30,
    TRANSLATE_DOMAIN = 31,
    TRANSLATE_STATEFUL = 32,
    TRANSLATE_FASTCGI = 33,
    TRANSLATE_VIEW = 34,
    TRANSLATE_USER_AGENT = 35,
    TRANSLATE_MAX_AGE = 36,
    TRANSLATE_VARY = 37,
};

struct beng_translation_header {
    uint16_t length;
    uint16_t command;
} __attr_packed;

#endif

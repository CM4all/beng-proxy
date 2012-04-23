/*
 * Definitions for the session file format.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_FILE_H
#define BENG_PROXY_SESSION_FILE_H

#include <stdint.h>

static const uint32_t MAGIC_FILE = 2461362038;
static const uint32_t MAGIC_SESSION = 663845834;
static const uint32_t MAGIC_WIDGET_SESSION = 983957472;
static const uint32_t MAGIC_COOKIE = 860919820;
static const uint32_t MAGIC_END_OF_RECORD = 1588449078;
static const uint32_t MAGIC_END_OF_LIST = 1556616445;

#endif

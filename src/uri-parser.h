/*
 * Dissect an URI into its parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_PARSER_H
#define __BENG_URI_PARSER_H

#include "strref.h"
#include "pool.h"

#include <stdbool.h>

struct parsed_uri {
    struct strref base;
    struct strref args;
    struct strref query;
};

bool
uri_parse(struct parsed_uri *dest, const char *src);

#endif

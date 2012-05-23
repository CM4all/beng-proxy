/*
 * Dissect an URI into its parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_PARSER_H
#define __BENG_URI_PARSER_H

#include "strref.h"

#include <stdbool.h>

/**
 * A splitted URI.
 */
struct parsed_uri {
    /**
     * The "base" URI that points to the real resource, without
     * dynamic arguments.
     */
    struct strref base;

    /**
     * The beng-proxy arguments, which were introduced by a semicolon
     * (without the semicolon).
     */
    struct strref args;

    /**
     * The URI portion after the arguments, including the leading
     * slash.
     */
    struct strref path_info;

    /**
     * The query string (without the question mark).
     */
    struct strref query;
};

/**
 * Split the URI into its parts.  The result contains pointers into
 * the original string.
 */
bool
uri_parse(struct parsed_uri *dest, const char *src);

#endif

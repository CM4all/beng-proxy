/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REGEX_H
#define BENG_PROXY_REGEX_H

#include <glib.h>

struct pool;

const char *
expand_string(struct pool *pool, const char *src,
              const GMatchInfo *match_info);

#endif

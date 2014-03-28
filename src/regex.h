/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REGEX_H
#define BENG_PROXY_REGEX_H

#include <glib.h>

struct pool;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @return NULL on error
 */
const char *
expand_string(struct pool *pool, const char *src,
              const GMatchInfo *match_info, GError **error_r);

/**
 * Like expand_string(), but unescape the substitutions with the '%'
 * URI method.
 *
 * @return NULL on error
 */
const char *
expand_string_unescaped(struct pool *pool, const char *src,
                        const GMatchInfo *match_info,
                        GError **error_r);

#ifdef __cplusplus
}
#endif

#endif

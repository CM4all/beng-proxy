/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REGEX_HXX
#define BENG_PROXY_REGEX_HXX

#include "glibfwd.hxx"

struct pool;

/**
 * @return nullptr on error
 */
const char *
expand_string(struct pool *pool, const char *src,
              const GMatchInfo *match_info, GError **error_r);

/**
 * Like expand_string(), but unescape the substitutions with the '%'
 * URI method.
 *
 * @return nullptr on error
 */
const char *
expand_string_unescaped(struct pool *pool, const char *src,
                        const GMatchInfo *match_info,
                        GError **error_r);

#endif

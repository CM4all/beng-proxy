/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PEXPAND_HXX
#define BENG_PROXY_PEXPAND_HXX

class AllocatorPtr;
class MatchInfo;
class Error;

/**
 * Throws std::runtime_error on error.
 */
const char *
expand_string(AllocatorPtr alloc, const char *src,
              const MatchInfo &match_info);

/**
 * Like expand_string(), but unescape the substitutions with the '%'
 * URI method.
 *
 * Throws std::runtime_error on error.
 */
const char *
expand_string_unescaped(AllocatorPtr alloc, const char *src,
                        const MatchInfo &match_info);

#endif

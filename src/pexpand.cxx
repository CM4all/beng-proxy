/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pexpand.hxx"
#include "expand.hxx"
#include "regex.hxx"
#include "pool.hxx"
#include "uri_escape.hxx"

#include <assert.h>
#include <string.h>

const char *
expand_string(struct pool *pool, const char *src,
              const MatchInfo &match_info, GError **error_r)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(match_info.IsDefined());

    const size_t length = ExpandStringLength(src, match_info, error_r);
    if (length == size_t(-1))
        return nullptr;

    const auto buffer = (char *)p_malloc(pool, length + 1);

    struct Result {
        char *q;

        explicit Result(char *_q):q(_q) {}

        void Append(char ch) {
            *q++ = ch;
        }

        void Append(const char *p) {
            q = stpcpy(q, p);
        }

        void Append(const char *p, size_t _length) {
            q = (char *)mempcpy(q, p, _length);
        }

        void AppendValue(const char *p, size_t _length) {
            Append(p, _length);
        }
    };

    Result result(buffer);
    if (!ExpandString(result, src, match_info, error_r))
        return nullptr;

    assert(result.q == buffer + length);
    *result.q = 0;

    return buffer;
}

const char *
expand_string_unescaped(struct pool *pool, const char *src,
                        const MatchInfo &match_info,
                        GError **error_r)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(match_info.IsDefined());

    const size_t length = ExpandStringLength(src, match_info, error_r);
    if (length == size_t(-1))
        return nullptr;

    const auto buffer = (char *)p_malloc(pool, length + 1);

    struct Result {
        char *q;

        explicit Result(char *_q):q(_q) {}

        void Append(char ch) {
            *q++ = ch;
        }

        void Append(const char *p) {
            q = stpcpy(q, p);
        }

        void Append(const char *p, size_t _length) {
            q = (char *)mempcpy(q, p, _length);
        }

        void AppendValue(const char *p, size_t _length) {
            q = uri_unescape(q, p, _length);
        }
    };

    Result result(buffer);
    if (!ExpandString(result, src, match_info, error_r))
        return nullptr;

    assert(result.q <= buffer + length);
    *result.q = 0;

    return buffer;
}

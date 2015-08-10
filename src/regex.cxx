/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "regex.hxx"
#include "expand.hxx"
#include "pool.hxx"
#include "uri_escape.hxx"
#include "util/Domain.hxx"
#include "util/Error.hxx"

#include <glib.h>

#include <assert.h>
#include <string.h>

static constexpr Domain regex_domain("regex");

bool
UniqueRegex::Compile(const char *pattern, bool capture, Error &error)
{
    GError *gerror = nullptr;
    bool success = Compile(pattern, capture, &gerror);
    if (!success) {
        error.Set(regex_domain, gerror->code, gerror->message);
        g_error_free(gerror);
    }

    return success;
}

size_t
ExpandStringLength(const char *src, const GMatchInfo *match_info,
                   GError **error_r)
{
    struct Result {
        size_t result = 0;

        void Append(gcc_unused char ch) {
            ++result;
        }

        void Append(const char *p) {
            result += strlen(p);
        }

        void Append(gcc_unused const char *p, size_t length) {
            result += length;
        }

        void AppendValue(gcc_unused char *p, size_t length) {
            result += length;
        }

        size_t Commit() const {
            return result;
        }
    };

    Result result;
    return ExpandString(result, src, match_info, error_r)
        ? result.Commit()
        : size_t(-1);
}

const char *
expand_string(struct pool *pool, const char *src,
              const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(match_info != nullptr);

    char *p = g_match_info_expand_references(match_info, src, error_r);
    if (p == nullptr)
        return nullptr;

    /* move result to the memory pool */
    char *q = p_strdup(pool, p);
    g_free(p);
    return q;
}

const char *
expand_string_unescaped(struct pool *pool, const char *src,
                        const GMatchInfo *match_info,
                        GError **error_r)
{
    assert(pool != nullptr);
    assert(src != nullptr);
    assert(match_info != nullptr);

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

        void AppendValue(char *p, size_t _length) {
            Append(p, uri_unescape_inplace(p, _length));
        }
    };

    Result result(buffer);
    if (!ExpandString(result, src, match_info, error_r))
        return nullptr;

    assert(result.q <= buffer + length);
    *result.q = 0;

    return buffer;
}

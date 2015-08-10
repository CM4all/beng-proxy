/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "regex.hxx"
#include "expand.hxx"
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

/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "regex.hxx"
#include "expand.hxx"
#include "util/Domain.hxx"
#include "util/Error.hxx"

#include <assert.h>
#include <string.h>

static constexpr Domain regex_domain("regex");

bool
UniqueRegex::Compile(const char *pattern, bool capture, Error &error)
{
    constexpr int default_options = PCRE_DOTALL|PCRE_NO_AUTO_CAPTURE;

    int options = default_options;
    if (capture)
        options &= ~PCRE_NO_AUTO_CAPTURE;

    const char *error_string;
    int error_offset;
    re = pcre_compile(pattern, options, &error_string, &error_offset, nullptr);
    if (re == nullptr) {
        error.Format(regex_domain, "Error in regex at offset %d: %s",
                     error_offset, error_string);
        return false;
    }

    extra = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error_string);
    if (extra == nullptr) {
        pcre_free(re);
        re = nullptr;
        error.Format(regex_domain, "Regex study error: %s", error_string);
        return false;
    }

    return true;
}

size_t
ExpandStringLength(const char *src, MatchInfo match_info,
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

        void AppendValue(gcc_unused const char *p, size_t length) {
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

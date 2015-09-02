/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REGEX_HXX
#define BENG_PROXY_REGEX_HXX

#include "util/ConstBuffer.hxx"

#include <pcre.h>

#include <assert.h>
#include <string.h>

#include <algorithm>

class Error;

class MatchInfo {
    friend class RegexPointer;

    static constexpr size_t OVECTOR_SIZE = 30;

    const char *s;
    int n;
    int ovector[OVECTOR_SIZE];

    explicit MatchInfo(const char *_s):s(_s) {}

public:
    MatchInfo() = default;

    constexpr bool IsDefined() const {
        return n >= 0;
    }

    ConstBuffer<char> GetCapture(unsigned i) const {
        assert(n >= 0);

        if (i >= unsigned(n))
            return { nullptr, 0 };

        int start = ovector[2 * i];
        if (start < 0)
            return { "", 0 };

        int end = ovector[2 * i + 1];
        assert(end >= start);

        return { s + start, size_t(end - start) };
    }
};

class RegexPointer {
protected:
    pcre *re = nullptr;
    pcre_extra *extra = nullptr;

    unsigned n_capture = 0;

public:
    constexpr bool IsDefined() const {
        return re != nullptr;
    }

    bool Match(const char *s) const {
        /* we don't need the data written to ovector, but PCRE can
           omit internal allocations if we pass a buffer to
           pcre_exec() */
        int ovector[MatchInfo::OVECTOR_SIZE];
        return pcre_exec(re, extra, s, strlen(s),
                         0, 0, ovector, MatchInfo::OVECTOR_SIZE) >= 0;
    }

    MatchInfo MatchCapture(const char *s) const {
        MatchInfo mi(s);
        mi.n = pcre_exec(re, extra, s, strlen(s),
                         0, 0, mi.ovector, mi.OVECTOR_SIZE);
        if (mi.n == 0)
            /* not enough room in the array - assume it's full */
            mi.n = mi.OVECTOR_SIZE / 3;
        else if (mi.n > 0 && n_capture >= unsigned(mi.n))
            /* in its return value, PCRE omits mismatching optional
               captures if (and only if) they are the last capture;
               this kludge works around this */
            mi.n = std::min<unsigned>(n_capture + 1, mi.OVECTOR_SIZE / 3);
        return mi;
    }
};

class UniqueRegex : public RegexPointer {
public:
    UniqueRegex() = default;

    UniqueRegex(UniqueRegex &&src):RegexPointer(src) {
        src.re = nullptr;
    }

    ~UniqueRegex() {
        pcre_free(re);
#ifdef PCRE_CONFIG_JIT
        pcre_free_study(extra);
#else
        pcre_free(extra);
#endif
    }

    UniqueRegex &operator=(UniqueRegex &&src) {
        std::swap(re, src.re);
        return *this;
    }

    bool Compile(const char *pattern, bool anchored, bool capture,
                 Error &error);
};

/**
 * Calculate the length of an expanded string.
 *
 * @return the length (without the null terminator) or size_t(-1) on
 * error
 */
size_t
ExpandStringLength(const char *src, MatchInfo match_info,
                   Error &error_r);

#endif

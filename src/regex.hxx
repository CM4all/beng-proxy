/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Utilities for dealing with regular expressions.
 */

#ifndef BENG_PROXY_REGEX_HXX
#define BENG_PROXY_REGEX_HXX

#include "util/StringView.hxx"

#include <pcre.h>

#include <assert.h>
#include <string.h>

#include <algorithm>

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

    StringView GetCapture(unsigned i) const {
        assert(n >= 0);

        if (i >= unsigned(n))
            return nullptr;

        int start = ovector[2 * i];
        if (start < 0)
            return StringView::Empty();

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

    UniqueRegex(const char *pattern, bool anchored, bool capture) {
        Compile(pattern, anchored, capture);
    }

    UniqueRegex(UniqueRegex &&src):RegexPointer(src) {
        src.re = nullptr;
        src.extra = nullptr;
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
        std::swap<RegexPointer>(*this, src);
        return *this;
    }

    /**
     * Throws std::runtime_error on error.
     */
    void Compile(const char *pattern, bool anchored, bool capture);
};

/**
 * Calculate the length of an expanded string.
 *
 * Throws std::runtime_error on error.
 *
 * @return the length (without the null terminator)
 */
size_t
ExpandStringLength(const char *src, const MatchInfo &match_info);

#endif

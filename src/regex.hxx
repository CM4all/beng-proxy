/*
 * Utilities for dealing with regular expressions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REGEX_HXX
#define BENG_PROXY_REGEX_HXX

#include <glib.h>

#include <algorithm>

class Error;

class MatchInfo {
protected:
    GMatchInfo *mi;

    explicit constexpr MatchInfo(GMatchInfo *_mi):mi(_mi) {}

public:
    MatchInfo() = default;
    MatchInfo(const MatchInfo &) = default;

    constexpr bool IsDefined() const {
        return mi != nullptr;
    }

    char *GetCapture(unsigned i) const {
        return g_match_info_fetch(mi, i);
    }

    void FreeCapture(char *c) const {
        g_free(c);
    }
};

class UniqueMatchInfo : public MatchInfo {
public:
    explicit UniqueMatchInfo(GMatchInfo *_mi):MatchInfo(_mi) {}

    UniqueMatchInfo(UniqueMatchInfo &&src):MatchInfo(src.mi) {
        src.mi = nullptr;
    }

    ~UniqueMatchInfo() {
        if (mi != nullptr)
            g_match_info_unref(mi);
    }

    UniqueMatchInfo &operator=(UniqueMatchInfo &&src) {
        std::swap(mi, src.mi);
        return *this;
    }
};

class RegexPointer {
protected:
    GRegex *re = nullptr;

    explicit constexpr RegexPointer(GRegex *_re):re(_re) {}

public:
    RegexPointer() = default;
    RegexPointer(const RegexPointer &) = default;

    constexpr bool IsDefined() const {
        return re != nullptr;
    }

    bool Match(const char *s) const {
        return g_regex_match(re, s, GRegexMatchFlags(0), nullptr);
    }

    UniqueMatchInfo MatchCapture(const char *s) const {
        GMatchInfo *mi = nullptr;
        if (!g_regex_match(re, s, GRegexMatchFlags(0), &mi)) {
            g_match_info_unref(mi);
            mi = nullptr;
        }

        return UniqueMatchInfo(mi);
    }
};

class UniqueRegex : public RegexPointer {
public:
    UniqueRegex() = default;

    UniqueRegex(UniqueRegex &&src):RegexPointer(src) {
        src.re = nullptr;
    }

    ~UniqueRegex() {
        if (re != nullptr)
            g_regex_unref(re);
    }

    UniqueRegex &operator=(UniqueRegex &&src) {
        std::swap(re, src.re);
        return *this;
    }

    bool Compile(const char *pattern, bool capture, GError **error_r) {
        constexpr GRegexCompileFlags default_compile_flags =
            GRegexCompileFlags(G_REGEX_DOTALL|
                               G_REGEX_RAW|G_REGEX_NO_AUTO_CAPTURE|
                               G_REGEX_OPTIMIZE);

        auto compile_flags = default_compile_flags;
        if (capture)
            compile_flags = GRegexCompileFlags(compile_flags &
                                               ~G_REGEX_NO_AUTO_CAPTURE);

        re = g_regex_new(pattern, compile_flags, GRegexMatchFlags(0),
                         error_r);
        return re != nullptr;
    }

    bool Compile(const char *pattern, bool capture, Error &error);
};

/**
 * Calculate the length of an expanded string.
 *
 * @return the length (without the null terminator) or size_t(-1) on
 * error
 */
size_t
ExpandStringLength(const char *src, MatchInfo match_info,
                   GError **error_r);

#endif

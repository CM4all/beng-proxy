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

    bool Match(const char *s, GMatchInfo **match_info=nullptr) const {
        return g_regex_match(re, s, GRegexMatchFlags(0), match_info);
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
            GRegexCompileFlags(G_REGEX_MULTILINE|G_REGEX_DOTALL|
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

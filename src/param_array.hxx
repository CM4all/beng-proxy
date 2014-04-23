/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PARAM_ARRAY_HXX
#define BENG_PROXY_PARAM_ARRAY_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <assert.h>
#include <stddef.h>

struct pool;

/**
 * An array of parameter strings.
 */
struct param_array {
    static constexpr size_t CAPACITY = 32;

    unsigned n;

    /**
     * Command-line arguments.
     */
    const char *values[CAPACITY];

    const char *expand_values[CAPACITY];

    void Init() {
        n = 0;
    }

    constexpr bool IsFull() const {
        return n == CAPACITY;
    }

    void CopyFrom(struct pool *pool, const struct param_array &src);

    void Append(const char *value) {
        assert(n <= CAPACITY);

        const unsigned i = n++;

        values[i] = value;
        expand_values[i] = nullptr;
    }

    bool CanSetExpand() const {
        assert(n <= CAPACITY);

        return n > 0 && expand_values[n - 1] == nullptr;
    }

    void SetExpand(const char *value) {
        assert(CanSetExpand());

        expand_values[n - 1] = value;
    }

    gcc_pure
    bool IsExpandable() const;

    bool Expand(struct pool *pool,
                const GMatchInfo *match_info, GError **error_r);
};

#endif

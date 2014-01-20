/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PARAM_ARRAY_H
#define BENG_PROXY_PARAM_ARRAY_H

#include <assert.h>
#include <stdbool.h>

struct pool;

#define PARAM_ARRAY_SIZE 32

/**
 * An array of parameter strings.
 */
struct param_array {
    unsigned n;

    /**
     * Command-line arguments.
     */
    const char *values[PARAM_ARRAY_SIZE];
};

static inline void
param_array_init(struct param_array *pa)
{
    pa->n = 0;
}

static inline bool
param_array_full(const struct param_array *pa)
{
    assert(pa->n <= PARAM_ARRAY_SIZE);

    return pa->n == PARAM_ARRAY_SIZE;
}

void
param_array_copy(struct pool *pool, struct param_array *dest,
                 const struct param_array *src);

static inline void
param_array_append(struct param_array *pa, const char *value)
{
    assert(pa->n <= PARAM_ARRAY_SIZE);

    pa->values[pa->n++] = value;
}

#endif

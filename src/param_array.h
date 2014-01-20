/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PARAM_ARRAY_H
#define BENG_PROXY_PARAM_ARRAY_H

#include <inline/compiler.h>

#include <glib.h>

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

    const char *expand_values[PARAM_ARRAY_SIZE];
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

    const unsigned i = pa->n++;

    pa->values[i] = value;
    pa->expand_values[i] = NULL;
}

static inline bool
param_array_can_set_expand(const struct param_array *pa)
{
    assert(pa->n <= PARAM_ARRAY_SIZE);

    return pa->n > 0 && pa->expand_values[pa->n - 1] == NULL;
}

static inline void
param_array_set_expand(struct param_array *pa, const char *value)
{
    assert(param_array_can_set_expand(pa));

    pa->expand_values[pa->n - 1] = value;
}

gcc_pure
bool
param_array_is_expandable(const struct param_array *pa);

bool
param_array_expand(struct pool *pool, struct param_array *pa,
                   const GMatchInfo *match_info, GError **error_r);

#endif

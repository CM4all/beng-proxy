/*
 * Transformations which can be applied to resources.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_TRANSFORMATION_H
#define BENG_TRANSFORMATION_H

#include "resource-address.h"

struct transformation {
    struct transformation *next;

    enum {
        TRANSFORMATION_PROCESS,
        TRANSFORMATION_FILTER,
    } type;

    union {
        struct {
            unsigned options;

            const char *domain;
        } processor;

        struct resource_address filter;
    } u;
};

/**
 * Returns true if the first "PROCESS" transformation in the chain (if
 * any) includes the "CONTAINER" processor option.
 */
bool
transformation_is_container(const struct transformation *t);

struct transformation *
transformation_dup(pool_t pool, const struct transformation *src);

struct transformation *
transformation_dup_chain(pool_t pool, const struct transformation *src);

#endif

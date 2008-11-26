/*
 * Transformations which can be applied to resources.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_TRANSFORMATION_H
#define BENG_TRANSFORMATION_H

#include "resource-address.h"

struct translate_transformation {
    struct translate_transformation *next;

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

struct translate_transformation *
transformation_dup(pool_t pool, const struct translate_transformation *src);

struct translate_transformation *
transformation_dup_chain(pool_t pool, const struct translate_transformation *src);

#endif

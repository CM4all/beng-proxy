/*
 * Transformations which can be applied to resources.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_TRANSFORMATION_H
#define BENG_TRANSFORMATION_H

#include "resource-address.h"

#include <inline/compiler.h>

struct pool;

struct transformation {
    struct transformation *next;

    enum {
        TRANSFORMATION_PROCESS,
        TRANSFORMATION_PROCESS_CSS,
        TRANSFORMATION_PROCESS_TEXT,
        TRANSFORMATION_FILTER,
    } type;

    union {
        struct {
            unsigned options;
        } processor;

        struct {
            unsigned options;
        } css_processor;

        struct resource_address filter;
    } u;
};

/**
 * Returns true if the first "PROCESS" transformation in the chain (if
 * any) includes the "CONTAINER" processor option.
 */
gcc_pure
bool
transformation_is_container(const struct transformation *t);

gcc_malloc
struct transformation *
transformation_dup(struct pool *pool, const struct transformation *src);

gcc_malloc
struct transformation *
transformation_dup_chain(struct pool *pool, const struct transformation *src);

#endif

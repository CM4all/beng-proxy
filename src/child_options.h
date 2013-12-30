/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_OPTIONS_H
#define BENG_PROXY_CHILD_OPTIONS_H

#include "jail.h"
#include "namespace_options.h"

/**
 * Options for launching a child process.
 */
struct child_options {
    struct namespace_options ns;

    struct jail_params jail;
};

static inline void
child_options_copy(struct pool *pool, struct child_options *dest,
                   const struct child_options *src)
{
    namespace_options_copy(&dest->ns, &src->ns);
    jail_params_copy(pool, &dest->jail, &src->jail);
}

#endif

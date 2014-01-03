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
child_options_init(struct child_options *options)
{
    namespace_options_init(&options->ns);
    jail_params_init(&options->jail);
}

static inline void
child_options_copy(struct pool *pool, struct child_options *dest,
                   const struct child_options *src)
{
    namespace_options_copy(&dest->ns, &src->ns);
    jail_params_copy(pool, &dest->jail, &src->jail);
}

static inline char *
child_options_id(const struct child_options *options, char *p)
{
    p = namespace_options_id(&options->ns, p);
    p = jail_params_id(&options->jail, p);
    return p;
}

#endif

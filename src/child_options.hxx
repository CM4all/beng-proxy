/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_OPTIONS_HXX
#define BENG_PROXY_CHILD_OPTIONS_HXX

#include "rlimit_options.h"
#include "namespace_options.h"
#include "jail.h"

/**
 * Options for launching a child process.
 */
struct child_options {
    struct rlimit_options rlimits;

    struct namespace_options ns;

    struct jail_params jail;
};

static inline void
child_options_init(struct child_options *options)
{
    rlimit_options_init(&options->rlimits);
    namespace_options_init(&options->ns);
    jail_params_init(&options->jail);
}

static inline void
child_options_copy(struct pool *pool, struct child_options *dest,
                   const struct child_options *src)
{
    rlimit_options_copy(&dest->rlimits, &src->rlimits);
    namespace_options_copy(pool, &dest->ns, &src->ns);
    jail_params_copy(pool, &dest->jail, &src->jail);
}

static inline char *
child_options_id(const struct child_options *options, char *p)
{
    p = rlimit_options_id(&options->rlimits, p);
    p = namespace_options_id(&options->ns, p);
    p = jail_params_id(&options->jail, p);
    return p;
}

#endif

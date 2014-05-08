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

    void Init() {
        rlimit_options_init(&rlimits);
        namespace_options_init(&ns);
        jail_params_init(&jail);
    }

    void CopyFrom(struct pool *pool, const struct child_options *src);

    char *MakeId(char *p) const;
};

#endif

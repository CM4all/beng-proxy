/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NAMESPACE_OPTIONS_H
#define BENG_PROXY_NAMESPACE_OPTIONS_H

#include <stdbool.h>

struct namespace_options {
    /**
     * Start the child process in a new user namespace?
     */
    bool enable_user;

    /**
     * Start the child process in a new network namespace?
     */
    bool enable_network;
};

static inline void
namespace_options_copy(struct namespace_options *dest,
                       const struct namespace_options *src)
{
    *dest = *src;
}

void
namespace_options_unshare(const struct namespace_options *options);

#endif

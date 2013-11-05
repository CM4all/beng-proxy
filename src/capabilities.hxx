/*
 * Utilities for Linux capabilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CAPABILITIES_HXX
#define BENG_PROXY_CAPABILITIES_HXX

#include <sys/capability.h>

/**
 * Prepare the setuid() call.  Configures beng-proxy to keep certain
 * capabilities after switching to an unprivileged uid.
 */
void
capabilities_pre_setuid();

/**
 * Call after setuid().
 */
void
capabilities_post_setuid(const cap_value_t *keep_list, unsigned n);

#endif

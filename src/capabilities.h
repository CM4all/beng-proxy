/*
 * Utilities for Linux capabilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CAPABILITIES_H
#define BENG_PROXY_CAPABILITIES_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Prepare the setuid() call.  Configures beng-proxy to keep certain
 * capabilities after switching to an unprivileged uid.
 */
void
capabilities_pre_setuid(void);

/**
 * Call after setuid().
 */
void
capabilities_post_setuid(void);

#ifdef __cplusplus
}
#endif

#endif

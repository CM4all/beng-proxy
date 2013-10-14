/*
 * Saving all sessions into a file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_SAVE_H
#define BENG_PROXY_SESSION_SAVE_H

#ifdef __cplusplus
extern "C" {
#endif

void
session_save_init(const char *path);

void
session_save_deinit(void);

#ifdef __cplusplus
}
#endif

#endif

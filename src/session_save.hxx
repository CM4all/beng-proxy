/*
 * Saving all sessions into a file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_SAVE_HXX
#define BENG_PROXY_SESSION_SAVE_HXX

void
session_save_init(const char *path);

void
session_save_deinit();

#endif

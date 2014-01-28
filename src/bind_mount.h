/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BIND_MOUNT_H
#define BENG_PROXY_BIND_MOUNT_H

void
bind_mount(const char *source, const char *target, int flags);

#endif

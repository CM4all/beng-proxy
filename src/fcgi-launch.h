/*
 * Launch FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_LAUNCH_H
#define BENG_PROXY_FCGI_LAUNCH_H

#include <glib.h>
#include <sys/types.h>

struct jail_params;

pid_t
fcgi_spawn_child(const struct jail_params *jail,
                 const char *executable_path, int fd,
                 GError **error_r);

#endif

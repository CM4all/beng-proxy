/*
 * Launch FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_LAUNCH_H
#define BENG_PROXY_FCGI_LAUNCH_H

#include <sys/types.h>

pid_t
fcgi_spawn_child(const char *executable_path, const char *jail_path, int fd);

#endif

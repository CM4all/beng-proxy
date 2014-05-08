/*
 * Launch FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_LAUNCH_H
#define BENG_PROXY_FCGI_LAUNCH_H

#include <inline/compiler.h>

struct jail_params;

#ifdef __cplusplus
extern "C" {
#endif

gcc_noreturn
void
fcgi_run(const struct jail_params *jail,
         const char *executable_path,
         const char *const*args, unsigned n_args);

#ifdef __cplusplus
}
#endif

#endif

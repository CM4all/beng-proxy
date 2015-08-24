/*
 * Launch "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_LAUNCH_HXX
#define BENG_PROXY_LHTTP_LAUNCH_HXX

#include <inline/compiler.h>

struct LhttpAddress;

gcc_noreturn
void
lhttp_run(const LhttpAddress *address, int fd);

#endif

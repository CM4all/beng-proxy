/*
 * Launch "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_LAUNCH_H
#define BENG_PROXY_LHTTP_LAUNCH_H

#include <inline/compiler.h>

struct lhttp_address;

gcc_noreturn
void
lhttp_run(const struct lhttp_address *address, int fd);

#endif

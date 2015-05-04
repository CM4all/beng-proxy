/*
 * Utilities for host names
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HOSTNAME_HXX
#define BENG_PROXY_HOSTNAME_HXX

#include <inline/compiler.h>

gcc_pure
bool
hostname_is_well_formed(const char *p);

#endif

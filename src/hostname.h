/*
 * Utilities for host names
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HOSTNAME_H
#define BENG_PROXY_HOSTNAME_H

#include <stdbool.h>

bool
hostname_is_well_formed(const char *p);

#endif

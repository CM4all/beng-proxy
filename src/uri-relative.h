/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URI_H
#define __BENG_URI_H

#include "pool.h"

const char *
uri_absolute(pool_t pool, const char *base, const char *uri, size_t length);

#endif

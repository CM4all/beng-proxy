/*
 * Extract parts of an URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_EXTRACT_H
#define BENG_PROXY_URI_EXTRACT_H

#include "pool.h"

const char *
uri_host_and_port(pool_t pool, const char *uri);

const char *
uri_path(const char *uri);

#endif

/*
 * Extract parts of an URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_EXTRACT_H
#define BENG_PROXY_URI_EXTRACT_H

#include <inline/compiler.h>

#include <stdbool.h>
#include <stddef.h>

struct pool;

#ifdef __cplusplus
extern "C" {
#endif

gcc_pure
bool
uri_has_protocol(const char *uri, size_t length);

gcc_pure gcc_malloc
const char *
uri_host_and_port(struct pool *pool, const char *uri);

gcc_pure
const char *
uri_path(const char *uri);

gcc_pure
const char *
uri_query_string(const char *uri);

#ifdef __cplusplus
}
#endif

#endif

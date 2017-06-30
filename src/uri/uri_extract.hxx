/*
 * Extract parts of an URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_EXTRACT_HXX
#define BENG_PROXY_URI_EXTRACT_HXX

#include "util/Compiler.h"

#include <stddef.h>

struct StringView;

gcc_pure
bool
uri_has_protocol(StringView uri);

/**
 * Does this URI have an authority part?
 */
gcc_pure
bool
uri_has_authority(StringView uri);

gcc_pure
StringView
uri_host_and_port(const char *uri);

/**
 * Returns the URI path (including the query string) or nullptr if the
 * given URI has no path.
 */
gcc_pure
const char *
uri_path(const char *uri);

gcc_pure
const char *
uri_query_string(const char *uri);

#endif

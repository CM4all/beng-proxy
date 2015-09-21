/*
 * Extract parts of an URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_EXTRACT_HXX
#define BENG_PROXY_URI_EXTRACT_HXX

#include <inline/compiler.h>

#include <stddef.h>

template<typename T> struct ConstBuffer;

gcc_pure
bool
uri_has_protocol(const char *uri, size_t length);

/**
 * Does this URI have an authority part?
 */
gcc_pure
bool
uri_has_authority(const char *uri, size_t length);

gcc_pure
ConstBuffer<char>
uri_host_and_port(const char *uri);

gcc_pure
const char *
uri_path(const char *uri);

gcc_pure
const char *
uri_query_string(const char *uri);

#endif

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PROCESSOR_HEADERS_HXX
#define BENG_PROXY_PROCESSOR_HEADERS_HXX

struct pool;
struct StringMap;

/**
 * Returns the processed response headers.
 */
StringMap *
processor_header_forward(struct pool *pool, const StringMap *headers);

#endif

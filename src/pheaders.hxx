/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PROCESSOR_HEADERS_HXX
#define BENG_PROXY_PROCESSOR_HEADERS_HXX

struct pool;

/**
 * Returns the processed response headers.
 */
struct strmap *
processor_header_forward(struct pool *pool, const struct strmap *headers);

#endif

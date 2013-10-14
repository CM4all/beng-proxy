/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PROCESSOR_HEADERS_H
#define BENG_PROXY_PROCESSOR_HEADERS_H

struct pool;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the processed response headers.
 */
struct strmap *
processor_header_forward(struct pool *pool, struct strmap *headers);

#ifdef __cplusplus
}
#endif

#endif

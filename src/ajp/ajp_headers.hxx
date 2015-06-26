/*
 * Serialize AJP request headers, deserialize response headers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_AJP_HEADERS_HXX
#define BENG_PROXY_AJP_HEADERS_HXX

struct pool;
struct GrowingBuffer;
struct strmap;
template<typename T> struct ConstBuffer;

/**
 * Serialize the specified headers to the buffer, but ignore "Content-Length".
 *
 * @return the number of headers which were written
 */
unsigned
serialize_ajp_headers(GrowingBuffer *gb, struct strmap *headers);

void
deserialize_ajp_headers(struct pool *pool, struct strmap *headers,
                        ConstBuffer<void> &input, unsigned num_headers);

void
deserialize_ajp_response_headers(struct pool *pool, struct strmap *headers,
                                 ConstBuffer<void> &input, unsigned num_headers);

#endif

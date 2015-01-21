/*
 * Serialize and deserialize FastCGI packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_SERIALIZE_HXX
#define BENG_PROXY_FCGI_SERIALIZE_HXX

#include <stdint.h>

struct GrowingBuffer;
struct strmap;
template<typename T> struct ConstBuffer;

/**
 * @param request_id the FastCGI request id in network byte order
 */
void
fcgi_serialize_params(GrowingBuffer *gb, uint16_t request_id, ...);

/**
 * @param request_id the FastCGI request id in network byte order
 */
void
fcgi_serialize_vparams(GrowingBuffer *gb, uint16_t request_id,
                       ConstBuffer<const char *> params);

void
fcgi_serialize_headers(GrowingBuffer *gb, uint16_t request_id,
                       const struct strmap *headers);

#endif

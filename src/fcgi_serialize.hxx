/*
 * Serialize and deserialize FastCGI packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_SERIALIZE_HXX
#define BENG_PROXY_FCGI_SERIALIZE_HXX

#include <stdint.h>

struct growing_buffer;
struct strmap;

/**
 * @param request_id the FastCGI request id in network byte order
 */
void
fcgi_serialize_params(struct growing_buffer *gb, uint16_t request_id, ...);

/**
 * @param request_id the FastCGI request id in network byte order
 */
void
fcgi_serialize_vparams(struct growing_buffer *gb, uint16_t request_id,
                       const char *const params[], unsigned num_params);

void
fcgi_serialize_headers(struct growing_buffer *gb, uint16_t request_id,
                       struct strmap *headers);

#endif

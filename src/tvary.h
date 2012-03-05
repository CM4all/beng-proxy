/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_VARY_H
#define BENG_PROXY_TRANSLATE_VARY_H

struct pool;
struct translate_response;
struct growing_buffer;

struct strmap *
add_translation_vary_header(struct pool *pool, struct strmap *headers,
                            const struct translate_response *response);

void
write_translation_vary_header(struct growing_buffer *headers,
                              const struct translate_response *response);

#endif

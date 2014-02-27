/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_VARY_HXX
#define BENG_PROXY_TRANSLATE_VARY_HXX

struct pool;
struct TranslateResponse;
struct growing_buffer;

struct strmap *
add_translation_vary_header(struct pool *pool, struct strmap *headers,
                            const TranslateResponse *response);

void
write_translation_vary_header(struct growing_buffer *headers,
                              const TranslateResponse *response);

#endif

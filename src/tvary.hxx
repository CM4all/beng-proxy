/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_VARY_HXX
#define BENG_PROXY_TRANSLATE_VARY_HXX

struct pool;
struct TranslateResponse;
struct GrowingBuffer;

struct strmap *
add_translation_vary_header(struct pool *pool, struct strmap *headers,
                            const TranslateResponse *response);

void
write_translation_vary_header(GrowingBuffer *headers,
                              const TranslateResponse *response);

#endif

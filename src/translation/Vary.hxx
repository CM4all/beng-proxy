/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_VARY_HXX
#define BENG_PROXY_TRANSLATE_VARY_HXX

class StringMap;
struct TranslateResponse;
class GrowingBuffer;

void
add_translation_vary_header(StringMap &headers,
                            const TranslateResponse &response);

void
write_translation_vary_header(GrowingBuffer &headers,
                              const TranslateResponse &response);

#endif

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATION_MARSHAL_HXX
#define BENG_PROXY_TRANSLATION_MARSHAL_HXX

#include <stdint.h>

struct pool;
struct TranslateRequest;
class GrowingBuffer;

GrowingBuffer
MarshalTranslateRequest(struct pool &pool, uint8_t PROTOCOL_VERSION,
                        const TranslateRequest &request);

#endif

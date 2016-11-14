/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATION_MARSHAL_HXX
#define BENG_PROXY_TRANSLATION_MARSHAL_HXX

#include "GrowingBuffer.hxx"
#include "util/ConstBuffer.hxx"

#include <beng-proxy/translation.h>

#include <utility>

#include <stdint.h>

struct TranslateRequest;
struct SocketAddress;

class TranslationMarshaller {
    GrowingBuffer buffer;

public:
    explicit TranslationMarshaller(struct pool &pool,
                                   size_t default_size=512)
        :buffer(pool, default_size) {}

    void Write(enum beng_translation_command command,
               ConstBuffer<void> payload=nullptr);

    template<typename T>
    void Write(enum beng_translation_command command,
               ConstBuffer<T> payload) {
        Write(command, payload.ToVoid());
    }

    void Write(enum beng_translation_command command,
               const char *payload);

    template<typename T>
    void WriteOptional(enum beng_translation_command command,
                       ConstBuffer<T> payload) {
        if (!payload.IsNull())
            Write(command, payload);
    }

    void WriteOptional(enum beng_translation_command command,
                       const char *payload) {
        if (payload != nullptr)
            Write(command, payload);
    }

    template<typename T>
    void WriteT(enum beng_translation_command command, const T &payload) {
        Write(command, ConstBuffer<T>(&payload, 1));
    }

    void Write16(enum beng_translation_command command, uint16_t payload) {
        WriteT<uint16_t>(command, payload);
    }

    void Write(enum beng_translation_command command,
               enum beng_translation_command command_string,
               SocketAddress address);

    void WriteOptional(enum beng_translation_command command,
                       enum beng_translation_command command_string,
                       SocketAddress address);

    GrowingBuffer Commit() {
        return std::move(buffer);
    }
};

GrowingBuffer
MarshalTranslateRequest(struct pool &pool, uint8_t PROTOCOL_VERSION,
                        const TranslateRequest &request);

#endif

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATION_MARSHAL_HXX
#define BENG_PROXY_TRANSLATION_MARSHAL_HXX

#include "GrowingBuffer.hxx"
#include "util/ConstBuffer.hxx"

#include <utility>

#include <stdint.h>

enum class TranslationCommand : uint16_t;
struct TranslateRequest;
class SocketAddress;

class TranslationMarshaller {
    GrowingBuffer buffer;

public:
    void Write(TranslationCommand command,
               ConstBuffer<void> payload=nullptr);

    template<typename T>
    void Write(TranslationCommand command,
               ConstBuffer<T> payload) {
        Write(command, payload.ToVoid());
    }

    void Write(TranslationCommand command,
               const char *payload);

    template<typename T>
    void WriteOptional(TranslationCommand command,
                       ConstBuffer<T> payload) {
        if (!payload.IsNull())
            Write(command, payload);
    }

    void WriteOptional(TranslationCommand command,
                       const char *payload) {
        if (payload != nullptr)
            Write(command, payload);
    }

    template<typename T>
    void WriteT(TranslationCommand command, const T &payload) {
        Write(command, ConstBuffer<T>(&payload, 1));
    }

    void Write16(TranslationCommand command, uint16_t payload) {
        WriteT<uint16_t>(command, payload);
    }

    void Write(TranslationCommand command,
               TranslationCommand command_string,
               SocketAddress address);

    void WriteOptional(TranslationCommand command,
                       TranslationCommand command_string,
                       SocketAddress address);

    GrowingBuffer Commit() {
        return std::move(buffer);
    }
};

GrowingBuffer
MarshalTranslateRequest(uint8_t PROTOCOL_VERSION,
                        const TranslateRequest &request);

#endif

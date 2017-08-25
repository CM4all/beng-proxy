/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
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

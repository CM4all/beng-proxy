/*
 * Parse translation response packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_READER_HXX
#define BENG_PROXY_TRANSLATE_READER_HXX

#include <beng-proxy/translation.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

class AllocatorPtr;

class TranslatePacketReader {
    enum class State {
        HEADER,
        PAYLOAD,
        COMPLETE,
    };

    State state = State::HEADER;

    struct beng_translation_header header;

    char *payload;
    size_t payload_position;

public:
    /**
     * Read a packet from the socket.
     *
     * @return the number of bytes consumed
     */
    size_t Feed(AllocatorPtr alloc, const uint8_t *data, size_t length);

    bool IsComplete() const {
        return state == State::COMPLETE;
    }

    enum beng_translation_command GetCommand() const {
        assert(IsComplete());

        return (enum beng_translation_command)header.command;
    }

    const void *GetPayload() const {
        assert(IsComplete());

        return payload != nullptr
            ? payload
            : "";
    }

    size_t GetLength() const {
        assert(IsComplete());

        return header.length;
    }
};

#endif

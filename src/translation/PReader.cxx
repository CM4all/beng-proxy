/*
 * Parse translation response packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "PReader.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

size_t
TranslatePacketReader::Feed(AllocatorPtr alloc,
                            const uint8_t *data, size_t length)
{
    assert(state == State::HEADER ||
           state == State::PAYLOAD ||
           state == State::COMPLETE);

    /* discard the packet that was completed (and consumed) by the
       previous call */
    if (state == State::COMPLETE)
        state = State::HEADER;

    size_t consumed = 0;

    if (state == State::HEADER) {
        if (length < sizeof(header))
            /* need more data */
            return 0;

        memcpy(&header, data, sizeof(header));

        if (header.length == 0) {
            payload = nullptr;
            state = State::COMPLETE;
            return sizeof(header);
        }

        consumed += sizeof(header);
        data += sizeof(header);
        length -= sizeof(header);

        state = State::PAYLOAD;

        payload_position = 0;
        payload = alloc.NewArray<char>(header.length + 1);
        payload[header.length] = 0;

        if (length == 0)
            return consumed;
    }

    assert(state == State::PAYLOAD);

    assert(payload_position < header.length);

    size_t nbytes = header.length - payload_position;
    if (nbytes > length)
        nbytes = length;

    memcpy(payload + payload_position, data, nbytes);
    payload_position += nbytes;
    if (payload_position == header.length)
        state = State::COMPLETE;

    consumed += nbytes;
    return consumed;
}

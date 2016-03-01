/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_PARSER_HXX
#define BENG_PROXY_SPAWN_PARSER_HXX

#include "util/ConstBuffer.hxx"

#include <algorithm>

#include <assert.h>
#include <stdint.h>
#include <string.h>

class MalformedSpawnPayloadError {};

class SpawnPayload {
    const uint8_t *begin, *const end;

public:
    explicit SpawnPayload(ConstBuffer<uint8_t> _payload)
        :begin(_payload.begin()), end(_payload.end()) {}

    bool IsEmpty() const {
        return begin == end;
    }

    size_t GetSize() const {
        return begin - end;
    }

    uint8_t ReadByte() {
        assert(!IsEmpty());
        return *begin++;
    }

    void Read(void *p, size_t size) {
        if (GetSize() < size)
            throw MalformedSpawnPayloadError();

        memcpy(p, begin, size);
        begin += size;
    }

    template<typename T>
    void ReadT(T &value_r) {
        Read(&value_r, sizeof(value_r));
    }

    void ReadInt(int &value_r) {
        ReadT(value_r);
    }

    const char *ReadString() {
        auto n = std::find(begin, end, 0);
        if (n == end)
            throw MalformedSpawnPayloadError();

        const char *value = (const char *)begin;
        begin = n + 1;
        return value;
    }
};

#endif

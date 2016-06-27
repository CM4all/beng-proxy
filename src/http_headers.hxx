/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_HEADERS_HXX
#define BENG_PROXY_HTTP_HEADERS_HXX

#include "strmap.hxx"
#include "growing_buffer.hxx"
#include "header_writer.hxx"
#include "header_parser.hxx"

#include <inline/compiler.h>

/**
 * A class that stores HTTP headers in a map and a buffer.  Some
 * libraries want a map, some want a buffer, and this class attempts
 * to give each of them what they can cope with best.
 */
class HttpHeaders {
    StringMap *map;

    GrowingBuffer *buffer;

public:
    HttpHeaders()
        :map(nullptr), buffer(nullptr) {}

    explicit HttpHeaders(StringMap *_map)
        :map(_map), buffer(nullptr) {}

    explicit HttpHeaders(StringMap &_map)
        :map(&_map), buffer(nullptr) {}

    explicit HttpHeaders(GrowingBuffer &_buffer)
        :map(nullptr), buffer(&_buffer) {}

    HttpHeaders(HttpHeaders &&) = default;
    HttpHeaders &operator=(HttpHeaders &&) = default;

    const StringMap *GetMap() const {
        return map;
    }

    StringMap &MakeMap(struct pool &pool) {
        if (map == nullptr)
            map = strmap_new(&pool);
        return *map;
    }

    StringMap &ToMap(struct pool &pool) {
        StringMap &m = MakeMap(pool);
        if (buffer != nullptr)
            header_parse_buffer(pool, m, *buffer);
        return m;
    }

    gcc_pure
    const char *Get(const char *key) const {
        return strmap_get_checked(map, key);
    }

    GrowingBuffer &MakeBuffer(struct pool &pool,
                                      size_t initial_size=1024) {
        if (buffer == nullptr)
            buffer = growing_buffer_new(&pool, initial_size);
        return *buffer;
    }

    void Write(struct pool &pool, const char *name, const char *value) {
        header_write(&MakeBuffer(pool), name, value);
    }

    /**
     * Move a (hop-by-hop) header from the map to the buffer.
     */
    void MoveToBuffer(struct pool &pool, const char *name) {
        const char *value = strmap_get_checked(map, name);
        if (value != nullptr)
            Write(pool, name, value);
    }

    void MoveToBuffer(struct pool &pool, const char *const*names) {
        if (map != nullptr)
            for (; *names != nullptr; ++names)
                MoveToBuffer(pool, *names);
    }

    GrowingBuffer ToBuffer(struct pool &pool,
                           size_t initial_size=2048) {
        GrowingBuffer &gb = MakeBuffer(pool, initial_size);
        if (map != nullptr)
            headers_copy_most(map, &gb);
        return std::move(gb);
    }
};

#endif

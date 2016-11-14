/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_HEADERS_HXX
#define BENG_PROXY_HTTP_HEADERS_HXX

#include "strmap.hxx"
#include "GrowingBuffer.hxx"
#include "header_writer.hxx"
#include "header_parser.hxx"

#include <inline/compiler.h>

/**
 * A class that stores HTTP headers in a map and a buffer.  Some
 * libraries want a map, some want a buffer, and this class attempts
 * to give each of them what they can cope with best.
 */
class HttpHeaders {
    StringMap map;

    GrowingBuffer buffer;

public:
    explicit HttpHeaders(struct pool &pool, size_t initial_size=1024)
        :map(pool), buffer(pool, initial_size) {}

    explicit HttpHeaders(StringMap &&_map, size_t initial_size=1024)
        :map(std::move(_map)), buffer(map.GetPool(), initial_size) {}

    explicit HttpHeaders(GrowingBuffer &&_buffer)
        :map(_buffer.GetPool()), buffer(std::move(_buffer)) {}

    HttpHeaders(HttpHeaders &&) = default;
    HttpHeaders &operator=(HttpHeaders &&) = default;

    struct pool &GetPool() {
        return map.GetPool();
    }

    const StringMap &GetMap() const {
        return map;
    }

    StringMap &&ToMap() && {
        header_parse_buffer(GetPool(), map, std::move(buffer));
        return std::move(map);
    }

    gcc_pure
    const char *Get(const char *key) const {
        return map.Get(key);
    }

    GrowingBuffer &GetBuffer() {
        return buffer;
    }

    GrowingBuffer MakeBuffer() {
        return std::move(buffer);
    }

    void Write(const char *name, const char *value) {
        header_write(buffer, name, value);
    }

    /**
     * Move a (hop-by-hop) header from the map to the buffer.
     */
    void MoveToBuffer(const char *name) {
        const char *value = map.Get(name);
        if (value != nullptr)
            Write(name, value);
    }

    void MoveToBuffer(const char *const*names) {
        for (; *names != nullptr; ++names)
            MoveToBuffer(*names);
    }

    GrowingBuffer &&ToBuffer() {
        headers_copy_most(map, buffer);
        return std::move(buffer);
    }
};

#endif

/*
 * Serialize and deserialize FastCGI packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_SERIALIZE_HXX
#define BENG_PROXY_FCGI_SERIALIZE_HXX

#include <stdint.h>
#include <stddef.h>

class GrowingBuffer;
class StringMap;
struct StringView;
template<typename T> struct ConstBuffer;

class FcgiRecordSerializer {
    GrowingBuffer &buffer;
    struct fcgi_record_header *const header;

public:
    FcgiRecordSerializer(GrowingBuffer &_buffer, uint8_t type,
                         uint16_t request_id_be) noexcept;

    GrowingBuffer &GetBuffer() {
        return buffer;
    }

    void Commit(size_t content_length) noexcept;
};

class FcgiParamsSerializer {
    FcgiRecordSerializer record;

    size_t content_length = 0;

public:
    FcgiParamsSerializer(GrowingBuffer &_buffer,
                         uint16_t request_id_be) noexcept;

    FcgiParamsSerializer &operator()(StringView name,
                                     StringView value) noexcept;

    void Headers(const StringMap &headers) noexcept;

    void Commit() noexcept {
        record.Commit(content_length);
    }
};

#endif

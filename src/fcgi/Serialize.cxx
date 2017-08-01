/*
 * Serialize and deserialize FastCGI packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Serialize.hxx"
#include "Protocol.hxx"
#include "GrowingBuffer.hxx"
#include "strmap.hxx"
#include "util/ConstBuffer.hxx"
#include "util/CharUtil.hxx"
#include "util/ByteOrder.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

static size_t
fcgi_serialize_length(GrowingBuffer &gb, size_t length)
{
    if (length < 0x80) {
        uint8_t buffer = (uint8_t)length;
        gb.Write(&buffer, sizeof(buffer));
        return sizeof(buffer);
    } else {
        /* XXX 31 bit overflow? */
        uint32_t buffer = ToBE32(length | 0x80000000);
        gb.Write(&buffer, sizeof(buffer));
        return sizeof(buffer);
    }
}

static size_t
fcgi_serialize_pair(GrowingBuffer &gb, StringView name,
                    StringView value)
{
    assert(!name.IsNull());

    size_t size = fcgi_serialize_length(gb, name.size);
    size += fcgi_serialize_length(gb, value.size);

    gb.Write(name.data, name.size);
    gb.Write(value.data, value.size);

    return size + name.size + value.size;
}

static size_t
fcgi_serialize_pair1(GrowingBuffer &gb, const char *name_and_value)
{
    assert(name_and_value != nullptr);

    size_t size, name_length, value_length;
    const char *value = strchr(name_and_value, '=');
    if (value != nullptr) {
        name_length = value - name_and_value;
        ++value;
        value_length = strlen(value);
    } else {
        name_length = strlen(name_and_value);
        value = "";
        value_length = 0;
    }

    size = fcgi_serialize_length(gb, name_length) +
        fcgi_serialize_length(gb, value_length);

    gb.Write(name_and_value, name_length);
    gb.Write(value, value_length);

    return size + name_length + value_length;
}

void
fcgi_serialize_params(GrowingBuffer &gb, uint16_t request_id, ...)
{
    size_t content_length = 0;

    struct fcgi_record_header *header = (struct fcgi_record_header *)
        gb.Write(sizeof(*header));
    header->version = FCGI_VERSION_1;
    header->type = FCGI_PARAMS;
    header->request_id = request_id;
    header->padding_length = 0;
    header->reserved = 0;

    va_list ap;
    va_start(ap, request_id);

    const char *name, *value;
    while ((name = va_arg(ap, const char *)) != nullptr) {
        value = va_arg(ap, const char *);
        content_length += fcgi_serialize_pair(gb, name, value);
    }

    va_end(ap);

    header->content_length = ToBE16(content_length);
}

void
fcgi_serialize_vparams(GrowingBuffer &gb, uint16_t request_id,
                       ConstBuffer<const char *> params)
{
    assert(!params.IsEmpty());

    struct fcgi_record_header *header = (struct fcgi_record_header *)
        gb.Write(sizeof(*header));
    header->version = FCGI_VERSION_1;
    header->type = FCGI_PARAMS;
    header->request_id = request_id;
    header->padding_length = 0;
    header->reserved = 0;

    size_t content_length = 0;
    for (auto i : params)
        content_length += fcgi_serialize_pair1(gb, i);

    header->content_length = ToBE16(content_length);
}

void
fcgi_serialize_headers(GrowingBuffer &gb, uint16_t request_id,
                       const StringMap &headers)
{
    struct fcgi_record_header *header = (struct fcgi_record_header *)
        gb.Write(sizeof(*header));
    header->version = FCGI_VERSION_1;
    header->type = FCGI_PARAMS;
    header->request_id = request_id;
    header->padding_length = 0;
    header->reserved = 0;

    size_t content_length = 0;
    char buffer[512] = "HTTP_";

    for (const auto &pair : headers) {
        if (strcmp(pair.key, "x-cm4all-https") == 0)
            /* this will be translated to HTTPS */
            continue;

        size_t i;

        for (i = 0; 5 + i < sizeof(buffer) - 1 && pair.key[i] != 0; ++i) {
            if (IsLowerAlphaASCII(pair.key[i]))
                buffer[5 + i] = (char)(pair.key[i] - 'a' + 'A');
            else if (IsUpperAlphaASCII(pair.key[i]) ||
                     IsDigitASCII(pair.key[i]))
                buffer[5 + i] = pair.key[i];
            else
                buffer[5 + i] = '_';
        }

        buffer[5 + i] = 0;

        content_length += fcgi_serialize_pair(gb, buffer, pair.value);
    }

    header->content_length = ToBE16(content_length);
}

/*
 * Serialize and deserialize FastCGI packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi_serialize.hxx"
#include "fcgi_protocol.h"
#include "growing_buffer.hxx"
#include "strmap.hxx"
#include "strutil.h"
#include "util/ConstBuffer.hxx"

#include <glib.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

static size_t
fcgi_serialize_length(struct growing_buffer *gb, size_t length)
{
    if (length < 0x80) {
        uint8_t buffer = (uint8_t)length;
        growing_buffer_write_buffer(gb, &buffer, sizeof(buffer));
        return sizeof(buffer);
    } else {
        /* XXX 31 bit overflow? */
        uint32_t buffer = GUINT32_TO_BE(length | 0x80000000);
        growing_buffer_write_buffer(gb, &buffer, sizeof(buffer));
        return sizeof(buffer);
    }
}

static size_t
fcgi_serialize_pair(struct growing_buffer *gb, const char *name,
                    const char *value)
{
    size_t size, name_length, value_length;

    assert(name != nullptr);

    if (value == nullptr)
        value = "";

    name_length = strlen(name);
    value_length = strlen(value);
    size = fcgi_serialize_length(gb, name_length) +
        fcgi_serialize_length(gb, value_length);

    growing_buffer_write_buffer(gb, name, name_length);
    growing_buffer_write_buffer(gb, value, value_length);

    return size + name_length + value_length;
}

static size_t
fcgi_serialize_pair1(struct growing_buffer *gb, const char *name_and_value)
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

    growing_buffer_write_buffer(gb, name_and_value, name_length);
    growing_buffer_write_buffer(gb, value, value_length);

    return size + name_length + value_length;
}

void
fcgi_serialize_params(struct growing_buffer *gb, uint16_t request_id, ...)
{
    size_t content_length = 0;

    struct fcgi_record_header *header = (struct fcgi_record_header *)
        growing_buffer_write(gb, sizeof(*header));
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

    header->content_length = GUINT16_TO_BE(content_length);
}

void
fcgi_serialize_vparams(struct growing_buffer *gb, uint16_t request_id,
                       ConstBuffer<const char *> params)
{
    assert(!params.IsEmpty());

    struct fcgi_record_header *header = (struct fcgi_record_header *)
        growing_buffer_write(gb, sizeof(*header));
    header->version = FCGI_VERSION_1;
    header->type = FCGI_PARAMS;
    header->request_id = request_id;
    header->padding_length = 0;
    header->reserved = 0;

    size_t content_length = 0;
    for (auto i : params)
        content_length += fcgi_serialize_pair1(gb, i);

    header->content_length = GUINT16_TO_BE(content_length);
}

void
fcgi_serialize_headers(struct growing_buffer *gb, uint16_t request_id,
                       struct strmap *headers)
{
    struct fcgi_record_header *header = (struct fcgi_record_header *)
        growing_buffer_write(gb, sizeof(*header));
    header->version = FCGI_VERSION_1;
    header->type = FCGI_PARAMS;
    header->request_id = request_id;
    header->padding_length = 0;
    header->reserved = 0;

    size_t content_length = 0;
    char buffer[512] = "HTTP_";
    const struct strmap_pair *pair;
    strmap_rewind(headers);
    while ((pair = strmap_next(headers)) != nullptr) {
        size_t i;

        for (i = 0; 5 + i < sizeof(buffer) - 1 && pair->key[i] != 0; ++i) {
            if (char_is_minuscule_letter(pair->key[i]))
                buffer[5 + i] = (char)(pair->key[i] - 'a' + 'A');
            else if (char_is_capital_letter(pair->key[i]) ||
                     char_is_digit(pair->key[i]))
                buffer[5 + i] = pair->key[i];
            else
                buffer[5 + i] = '_';
        }

        buffer[5 + i] = 0;

        content_length += fcgi_serialize_pair(gb, buffer, pair->value);
    }

    header->content_length = GUINT16_TO_BE(content_length);
}

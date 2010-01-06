/*
 * Serialize and deserialize FastCGI packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-serialize.h"
#include "fcgi-protocol.h"
#include "growing-buffer.h"

#include <glib.h>

#include <stdint.h>
#include <string.h>

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

    assert(name != NULL);

    if (value == NULL)
        value = "";

    name_length = strlen(name);
    value_length = strlen(value);
    size = fcgi_serialize_length(gb, name_length) +
        fcgi_serialize_length(gb, value_length);

    growing_buffer_write_buffer(gb, name, name_length);
    growing_buffer_write_buffer(gb, value, value_length);

    return size + name_length + value_length;
}

void
fcgi_serialize_params(struct growing_buffer *gb, const char *name,
                      const char *value)
{
    struct fcgi_record_header *header;
    size_t content_length;

    header = growing_buffer_write(gb, sizeof(*header));
    header->version = FCGI_VERSION_1;
    header->type = FCGI_PARAMS;
    header->request_id = GUINT16_TO_BE(1);
    header->padding_length = 0;
    header->reserved = 0;

    content_length = fcgi_serialize_pair(gb, name, value);
    header->content_length = GUINT16_TO_BE(content_length);
}

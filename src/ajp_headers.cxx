/*
 * Serialize AJP request headers, deserialize response headers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp_headers.hxx"
#include "ajp-protocol.h"
#include "ajp_serialize.hxx"
#include "serialize.hxx"
#include "growing_buffer.hxx"
#include "strmap.hxx"
#include "strutil.h"
#include "pool.h"
#include "util/ConstBuffer.hxx"

#include <assert.h>

static bool
serialize_ajp_header_name(struct growing_buffer *gb, const char *name)
{
    enum ajp_header_code code;

    code = ajp_encode_header_name(name);
    if (code == AJP_HEADER_CONTENT_LENGTH)
        return false;

    if (code == AJP_HEADER_NONE)
        serialize_ajp_string(gb, name);
    else
        serialize_ajp_integer(gb, code);

    return true;
}

unsigned
serialize_ajp_headers(struct growing_buffer *gb, struct strmap *headers)
{
    const struct strmap_pair *pair;
    unsigned n = 0;

    strmap_rewind(headers);
    while ((pair = strmap_next(headers)) != nullptr) {
        if (serialize_ajp_header_name(gb, pair->key)) {
            serialize_ajp_string(gb, pair->value);
            ++n;
        }
    }

    return n;
}

static void
SkipFront(ConstBuffer<void> &input, size_t n)
{
    assert(input.size >= n);

    input.data = (const uint8_t *)input.data + n;
    input.size -= n;
}

void
deserialize_ajp_headers(struct pool *pool, struct strmap *headers,
                        ConstBuffer<void> &input, unsigned num_headers)
{
    while (num_headers-- > 0) {
        unsigned length = deserialize_uint16(input);
        const char *name, *value;
        char *lname;

        if (input.IsNull())
            break;

        if (length >= AJP_HEADER_CODE_START) {
            name = ajp_decode_header_name((enum ajp_header_code)length);
            if (name == nullptr) {
                /* unknown - ignore it, it's the best we can do now */
                deserialize_ajp_string(input);
                continue;
            }
        } else {
            const char *data = (const char *)input.data;
            if (length >= input.size || data[length] != 0)
                /* buffer overflow */
                break;

            name = data;
            SkipFront(input, length + 1);
        }

        value = deserialize_ajp_string(input);
        if (value == nullptr)
            break;

        assert(name != nullptr);

        lname = p_strdup(pool, name);
        str_to_lower(lname);

        strmap_add(headers, lname, p_strdup(pool, value));
    }
}

void
deserialize_ajp_response_headers(struct pool *pool, struct strmap *headers,
                                 ConstBuffer<void> &input, unsigned num_headers)
{
    while (num_headers-- > 0) {
        unsigned length = deserialize_uint16(input);
        const char *name, *value;
        char *lname;

        if (input.IsNull())
            break;

        if (length >= AJP_RESPONSE_HEADER_CODE_START) {
            name = ajp_decode_response_header_name((enum ajp_response_header_code)length);
            if (name == nullptr) {
                /* unknown - ignore it, it's the best we can do now */
                deserialize_ajp_string(input);
                continue;
            }
        } else {
            const char *data = (const char *)input.data;
            if (length >= input.size || data[length] != 0)
                /* buffer overflow */
                break;

            name = data;
            SkipFront(input, length + 1);
        }

        value = deserialize_ajp_string(input);
        if (value == nullptr)
            break;

        assert(name != nullptr);

        lname = p_strdup(pool, name);
        str_to_lower(lname);

        strmap_add(headers, lname, p_strdup(pool, value));
    }
}

/*
 * Serialize AJP request headers, deserialize response headers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp-headers.h"
#include "ajp-protocol.h"
#include "ajp-serialize.h"
#include "serialize.h"
#include "growing-buffer.h"
#include "strmap.h"
#include "strref.h"
#include "strutil.h"

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
    while ((pair = strmap_next(headers)) != NULL) {
        if (serialize_ajp_header_name(gb, pair->key)) {
            serialize_ajp_string(gb, pair->value);
            ++n;
        }
    }

    return n;
}

void
deserialize_ajp_headers(pool_t pool, struct strmap *headers,
                        struct strref *input, unsigned num_headers)
{
    while (num_headers-- > 0) {
        unsigned length = deserialize_uint16(input);
        const char *name, *value;
        char *lname;

        if (strref_is_null(input))
            break;

        if (length >= AJP_HEADER_CODE_START) {
            name = ajp_decode_header_name(length);
            if (name == NULL) {
                /* unknown - ignore it, it's the best we can do now */
                deserialize_ajp_string(input);
                continue;
            }
        } else {
            name = input->data;
            strref_skip(input, length + 1);
        }

        value = deserialize_ajp_string(input);
        if (value == NULL)
            break;

        assert(name != NULL);

        lname = p_strdup(pool, name);
        str_to_lower(lname);

        strmap_add(headers, lname, value);
    }
}

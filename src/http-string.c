/*
 * HTTP string utilities according to RFC 2616 2.2.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-string.h"
#include "strref2.h"

void
http_next_token(struct strref *input, struct strref *value)
{
    value->length = 0;
    value->data = input->data;

    while (value->length < input->length &&
           char_is_http_token(input->data[value->length]))
        ++value->length;

    if (value->length > 0)
        strref_skip(input, value->length);
}

void
http_next_quoted_string(pool_t pool, struct strref *input, struct strref *value)
{
    char *dest = p_malloc(pool, input->length); /* XXX optimize memory consumption */
    size_t pos = 1;

    value->length = 0;
    value->data = dest;

    while (pos < input->length) {
        if (input->data[pos] == '\\') {
            ++pos;
            if (pos < input->length)
                dest[value->length++] = input->data[pos++];
        } else if (input->data[pos] == '"') {
            ++pos;
            break;
        } else if (char_is_http_text(input->data[pos])) {
            dest[value->length++] = input->data[pos++];
        } else {
            ++pos;
        }
    }

    strref_skip(input, pos);
}

void
http_next_value(pool_t pool, struct strref *input, struct strref *value)
{
    if (!strref_is_empty(input) && input->data[0] == '"')
        http_next_quoted_string(pool, input, value);
    else
        http_next_token(input, value);
}

void
http_next_name_value(pool_t pool, struct strref *input,
                     struct strref *name, struct strref *value)
{
    http_next_token(input, name);
    if (strref_is_empty(name))
        return;

    strref_ltrim(input);
    if (!strref_is_empty(input) && input->data[0] == '=') {
        strref_skip(input, 1);
        strref_ltrim(input);
        http_next_value(pool, input, value);
    } else
        strref_clear(value);
}

size_t
http_quote_string(char *dest, const struct strref *src)
{
    size_t dest_pos = 0, src_pos = 0;

    dest[dest_pos++] = '"';

    while (src_pos < src->length) {
        if (src->data[src_pos] == '"' || src->data[src_pos] == '\\') {
            dest[dest_pos++] = '\\';
            dest[dest_pos++] = src->data[src_pos++];
        } else if (char_is_http_text(src->data[src_pos]))
            dest[dest_pos++] = src->data[src_pos++];
        else
            /* XXX how to handle invalid characters? */
            break;
    }

    dest[dest_pos++] = '"';
    return dest_pos;
}

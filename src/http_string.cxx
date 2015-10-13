/*
 * HTTP string utilities according to RFC 2616 2.2.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_string.hxx"
#include "pool.hxx"

void
http_next_token(StringView &input, StringView &value)
{
    value.size = 0;
    value.data = input.data;

    while (value.size < input.size &&
           char_is_http_token(input[value.size]))
        ++value.size;

    if (value.size > 0)
        input.skip_front(value.size);
}

void
http_next_quoted_string(struct pool &pool, StringView &input,
                        StringView &value)
{
    char *dest = (char *)p_malloc(&pool, input.size); /* TODO: optimize memory consumption */
    size_t pos = 1;

    value.size = 0;
    value.data = dest;

    while (pos < input.size) {
        if (input[pos] == '\\') {
            ++pos;
            if (pos < input.size)
                dest[value.size++] = input[pos++];
        } else if (input[pos] == '"') {
            ++pos;
            break;
        } else if (char_is_http_text(input[pos])) {
            dest[value.size++] = input[pos++];
        } else {
            ++pos;
        }
    }

    input.skip_front(pos);
}

void
http_next_value(struct pool &pool, StringView &input, StringView &value)
{
    if (!input.IsEmpty() && input.front() == '"')
        http_next_quoted_string(pool, input, value);
    else
        http_next_token(input, value);
}

void
http_next_name_value(struct pool &pool, StringView &input,
                     StringView &name, StringView &value)
{
    http_next_token(input, name);
    if (name.IsEmpty())
        return;

    input.StripLeft();
    if (!input.IsEmpty() && input.front() == '=') {
        input.pop_front();
        input.StripLeft();

        http_next_value(pool, input, value);
    } else
        value = nullptr;
}

size_t
http_quote_string(char *dest, const StringView src)
{
    size_t dest_pos = 0, src_pos = 0;

    dest[dest_pos++] = '"';

    while (src_pos < src.size) {
        if (src[src_pos] == '"' || src[src_pos] == '\\') {
            dest[dest_pos++] = '\\';
            dest[dest_pos++] = src[src_pos++];
        } else if (char_is_http_text(src[src_pos]))
            dest[dest_pos++] = src[src_pos++];
        else
            /* ignore invalid characters */
            ++src_pos;
    }

    dest[dest_pos++] = '"';
    return dest_pos;
}

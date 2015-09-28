/*
 * Escaping with a static destination buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "escape_static.hxx"
#include "escape_class.hxx"
#include "util/StringView.hxx"

static char buffer[4096];

const char *
unescape_static(const struct escape_class *cls, StringView p)
{
    if (p.size >= sizeof(buffer))
        return nullptr;

    size_t l = unescape_buffer(cls, p, buffer);
    buffer[l] = 0;
    return buffer;
}

const char *
escape_static(const struct escape_class *cls, StringView p)
{
    size_t l = escape_size(cls, p);
    if (l >= sizeof(buffer))
        return nullptr;

    l = escape_buffer(cls, p, buffer);
    buffer[l] = 0;
    return buffer;
}

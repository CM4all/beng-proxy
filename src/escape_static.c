/*
 * Escaping with a static destination buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "escape_static.h"
#include "escape_class.h"

static char buffer[4096];

const char *
unescape_static(const struct escape_class *class,
                const char *p, size_t length)
{
    if (length >= sizeof(buffer))
        return NULL;

    size_t l = unescape_buffer(class, p, length, buffer);
    buffer[l] = 0;
    return buffer;
}

const char *
escape_static(const struct escape_class *class,
              const char *p, size_t length)
{
    size_t l = escape_size(class, p, length);
    if (l >= sizeof(buffer))
        return NULL;

    l = escape_buffer(class, p, length, buffer);
    buffer[l] = 0;
    return buffer;
}

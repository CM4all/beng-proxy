/*
 * istream implementation which reads from a string.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

#include <string.h>

struct istream *
istream_string_new(struct pool *pool, const char *s)
{
    return istream_memory_new(pool, s, strlen(s));
}

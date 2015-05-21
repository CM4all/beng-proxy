/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_string.hxx"
#include "istream_memory.hxx"

#include <string.h>

struct istream *
istream_string_new(struct pool *pool, const char *s)
{
    return istream_memory_new(pool, s, strlen(s));
}

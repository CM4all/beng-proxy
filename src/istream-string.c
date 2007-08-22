/*
 * istream implementation which reads from a string.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

#include <string.h>

istream_t
istream_string_new(pool_t pool, const void *s)
{
    return istream_memory_new(pool, s, strlen(s));
}

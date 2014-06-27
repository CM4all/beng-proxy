/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "istream-internal.h"
#include "istream-new.h"

istream::istream(const struct istream_class &cls, struct pool &pool)
{
    istream_init(this, &cls, &pool);
}

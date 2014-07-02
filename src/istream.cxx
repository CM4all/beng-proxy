/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "istream-internal.h"
#include "istream-new.h"

istream::istream(const struct istream_class &_cls, struct pool &_pool)
{
    istream_init(this, &_cls, &_pool);
}

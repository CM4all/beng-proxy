/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.hxx"
#include "istream_internal.hxx"
#include "istream_new.hxx"

istream::istream(struct pool &_pool, const struct istream_class &_cls)
{
    istream_init(this, &_cls, &_pool);
}

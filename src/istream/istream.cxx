/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.hxx"
#include "istream_invoke.hxx"
#include "istream_new.hxx"

istream::istream(struct pool &_pool)
{
    istream_init(this, &_pool);
}

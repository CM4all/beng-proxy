/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_HXX
#define ISTREAM_HXX

#include "Handler.hxx"
#include "istream_oo.hxx"
#include "pool.h"
#include "FdType.hxx"

#include <inline/compiler.h>

#include <assert.h>

static inline void
istream_free(Istream **istream_r)
{
    auto *istream = *istream_r;
    *istream_r = nullptr;
    istream->Close();
}

/**
 * Free an istream which was never used, i.e. it does not have a
 * handler yet.
 */
static inline void
istream_free_unused(Istream **istream_r)
{
    assert(istream_r != nullptr);
    assert(*istream_r != nullptr);
    assert(!(*istream_r)->HasHandler());

    istream_free(istream_r);
}

#endif

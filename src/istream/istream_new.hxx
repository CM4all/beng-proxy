/*
 * new() helpers for istream implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_NEW_H
#define __BENG_ISTREAM_NEW_H

#include <inline/valgrind.h>

static inline void
istream_init_impl(struct istream *istream,
                  struct pool *pool TRACE_ARGS_DECL)
{
    istream->pool = pool;

    pool_ref_fwd(pool);
}

#define istream_init(istream, pool) istream_init_impl(istream, pool TRACE_ARGS)

static inline void
istream_deinit_impl(struct istream *istream TRACE_ARGS_DECL)
{
    assert(istream != nullptr);
    assert(!istream->destroyed);

    struct pool *pool = istream->pool;

#ifndef NDEBUG
    /* poison the istream struct (but not its implementation
       properties), so it cannot be used later; this does not actually
       erase data, it will just give a hint to valgrind */
    VALGRIND_MAKE_MEM_UNDEFINED(istream, sizeof(*istream));

    istream->pool = pool;
    istream->destroyed = true;
#endif

    pool_unref_fwd(pool);
}

#define istream_deinit(istream) istream_deinit_impl(istream TRACE_ARGS)

#endif

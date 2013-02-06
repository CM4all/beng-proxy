/*
 * new() helpers for istream implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_NEW_H
#define __BENG_ISTREAM_NEW_H

#include <inline/valgrind.h>

static inline void
istream_init_impl(struct istream *istream, const struct istream_class *cls,
                  struct pool *pool TRACE_ARGS_DECL)
{
    istream->pool = pool;
    istream->cls = cls;
    istream->handler = NULL;
    istream->handler_direct = 0;

#ifndef NDEBUG
    istream->reading = false;
    istream->destroyed = false;
    istream->closing = false;
    istream->eof = false;
    istream->in_data = false;
    istream->available_full_set = false;
    istream->data_available = 0;
    istream->available_partial = 0;
    istream->available_full = 0;
#endif

    pool_ref_fwd(pool);
}

#define istream_init(istream, cls, pool) istream_init_impl(istream, cls, pool TRACE_ARGS)

gcc_malloc
static inline struct istream *
istream_new_impl(struct pool *pool,
                 const struct istream_class *cls, size_t size
                 TRACE_ARGS_DECL)
{
    struct istream *istream;

    assert(size >= sizeof(*istream));

#ifdef ISTREAM_POOL
    pool = pool_new_libc(pool, "istream");
#endif

    istream = p_malloc_fwd(pool, size);
    istream_init_impl(istream, cls, pool TRACE_ARGS_FWD);

#ifdef ISTREAM_POOL
    pool_unref(pool);
#endif

    return istream;
}

#define istream_new(pool, cls, size) istream_new_impl(pool, cls, size TRACE_ARGS)

#define istream_new_macro(pool, class_name) \
    ((struct istream_ ## class_name *) istream_new(pool, &istream_ ## class_name, sizeof(struct istream_ ## class_name)))

static inline void
istream_deinit_impl(struct istream *istream TRACE_ARGS_DECL)
{
    assert(istream != NULL);
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

static inline void
istream_deinit_eof_impl(struct istream *istream TRACE_ARGS_DECL)
{
    istream_invoke_eof(istream);
    istream_deinit_impl(istream TRACE_ARGS_FWD);
}

#define istream_deinit_eof(istream) istream_deinit_eof_impl(istream TRACE_ARGS)

static inline void
istream_deinit_abort_impl(struct istream *istream, GError *error TRACE_ARGS_DECL)
{
    istream_invoke_abort(istream, error);
    istream_deinit_impl(istream TRACE_ARGS_FWD);
}

#define istream_deinit_abort(istream, error) istream_deinit_abort_impl(istream, error TRACE_ARGS)

#endif

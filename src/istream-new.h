/*
 * new() helpers for istream implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_NEW_H
#define __BENG_ISTREAM_NEW_H

#include <inline/poison.h>

static inline void
istream_init_impl(struct istream *istream, const struct istream *class,
                  pool_t pool TRACE_ARGS_DECL)
{
    *istream = *class;

    istream->pool = pool;
    pool_ref_fwd(pool);
}

#define istream_init(istream, class, pool) istream_init_impl(istream, class, pool TRACE_ARGS)

static inline struct istream *
istream_new_impl(pool_t pool, 
                 const struct istream *class, size_t size
                 TRACE_ARGS_DECL)
{
    struct istream *istream;

    assert(size >= sizeof(*istream));

#ifdef ISTREAM_POOL
    pool = pool_new_libc(pool, "istream");
#endif

    istream = p_malloc(pool, size);
    istream_init_impl(istream, class, pool TRACE_ARGS_FWD);

#ifdef ISTREAM_POOL
    pool_unref(pool);
#endif

    return istream;
}

#define istream_new(pool, class, size) istream_new_impl(pool, class, size TRACE_ARGS)

#define istream_new_macro(pool, class_name) \
    ((struct istream_ ## class_name *) istream_new(pool, &istream_ ## class_name, sizeof(struct istream_ ## class_name)))

static inline void
istream_deinit_impl(struct istream *istream TRACE_ARGS_DECL)
{
    pool_t pool = istream->pool;

#ifndef NDEBUG
    /* poison the istream struct (but not its implementation
       properties), so it cannot be used later */
    poison_noaccess(istream, sizeof(*istream));
    poison_undefined(&istream->pool, sizeof(istream->pool));
    istream->pool = pool;

    poison_undefined(&istream->destroyed, sizeof(istream->destroyed));
    istream->destroyed = true;
#endif

    pool_unref_fwd(pool);
}

#define istream_deinit(istream) istream_deinit_impl(istream TRACE_ARGS)

static __attr_always_inline void
istream_deinit_eof_impl(struct istream *istream TRACE_ARGS_DECL)
{
    istream_invoke_eof(istream);
    istream_deinit_impl(istream TRACE_ARGS_FWD);
}

#define istream_deinit_eof(istream) istream_deinit_eof_impl(istream TRACE_ARGS)

static __attr_always_inline void
istream_deinit_abort_impl(struct istream *istream TRACE_ARGS_DECL)
{
    istream_invoke_abort(istream);
    istream_deinit_impl(istream TRACE_ARGS_FWD);
}

#define istream_deinit_abort(istream) istream_deinit_abort_impl(istream TRACE_ARGS)

#endif

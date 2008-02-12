/*
 * Asynchronous input stream API, reference management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_REF_H
#define __BENG_ISTREAM_REF_H

static inline void
istream_free_unref_impl(istream_t *istream_r TRACE_ARGS_DECL)
{
    pool_t pool = istream_pool(*istream_r);
    istream_free(istream_r);
    pool_unref_fwd(pool);
}

#define istream_free_unref(istream_r) istream_free_unref_impl(istream_r TRACE_ARGS)

static inline void
istream_free_unref_handler_impl(istream_t *istream_r TRACE_ARGS_DECL)
{
    istream_handler_clear(*istream_r);
    istream_free_unref_impl(istream_r TRACE_ARGS_FWD);
}

#define istream_free_unref_handler(istream_r) istream_free_unref_handler_impl(istream_r TRACE_ARGS)

static inline void
istream_assign_ref_impl(istream_t *istream_r, istream_t _istream TRACE_ARGS_DECL)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    *istream_r = _istream;
    pool_ref_fwd(istream->pool);
}

#define istream_assign_ref(istream_r, _istream) istream_assign_ref_impl(istream_r, _istream TRACE_ARGS)

static inline void
istream_assign_ref_handler_impl(istream_t *istream_r, istream_t istream,
                                const struct istream_handler *handler,
                                void *handler_ctx,
                                istream_direct_t handler_direct
                                TRACE_ARGS_DECL)
{
    istream_assign_ref_impl(istream_r, istream TRACE_ARGS_FWD);
    istream_handler_set(istream, handler, handler_ctx, handler_direct);
}

#define istream_assign_ref_handler(istream_r, istream, handler, handler_ctx, handler_direct) \
    istream_assign_ref_handler_impl(istream_r, istream, handler, handler_ctx, handler_direct TRACE_ARGS)

static inline void
istream_clear_unref_impl(istream_t *istream_r TRACE_ARGS_DECL)
{
    struct istream *istream = _istream_opaque_cast(*istream_r);
    *istream_r = NULL;
    pool_unref_fwd(istream->pool);
}

#define istream_clear_unref(istream_r) istream_clear_unref_impl(istream_r TRACE_ARGS)

static inline void
istream_clear_unref_handler_impl(istream_t *istream_r TRACE_ARGS_DECL)
{
    struct istream *istream = _istream_opaque_cast(*istream_r);
    *istream_r = NULL;
    istream_handler_clear(istream_struct_cast(istream));
    pool_unref_fwd(istream->pool);
}

#define istream_clear_unref_handler(istream_r) istream_clear_unref_handler_impl(istream_r TRACE_ARGS)

#endif

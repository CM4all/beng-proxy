/*
 * Asynchronous input stream API, reference management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_REF_H
#define __BENG_ISTREAM_REF_H

static inline void
istream_free_unref(istream_t *istream_r)
{
    pool_t pool = istream_pool(*istream_r);
    istream_free(istream_r);
    pool_unref(pool);
}

static inline void
istream_assign_ref(istream_t *istream_r, istream_t _istream)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    *istream_r = _istream;
    pool_ref(istream->pool);
}

static inline void
istream_assign_ref_handler(istream_t *istream_r, istream_t istream,
                           const struct istream_handler *handler,
                           void *handler_ctx,
                           istream_direct_t handler_direct)
{
    istream_assign_ref(istream_r, istream);
    istream_handler_set(istream, handler, handler_ctx, handler_direct);
}

static inline void
istream_clear_unref(istream_t *istream_r)
{
    struct istream *istream = _istream_opaque_cast(*istream_r);
    *istream_r = NULL;
    pool_unref(istream->pool);
}

static inline void
istream_clear_unref_handler(istream_t *istream_r)
{
    struct istream *istream = _istream_opaque_cast(*istream_r);
    *istream_r = NULL;
    istream_handler_clear(istream_struct_cast(istream));
    pool_unref(istream->pool);
}

#endif

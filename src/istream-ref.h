/*
 * Asynchronous input stream API, reference management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_REF_H
#define __BENG_ISTREAM_REF_H

#ifdef DEBUG_POOL_REF
#define x_pool_ref(...) pool_ref_debug(__VA_ARGS__, file, line)
#define x_pool_unref(...) pool_unref_debug(__VA_ARGS__, file, line)
#else
#define x_pool_ref(...) pool_ref(__VA_ARGS__)
#define x_pool_unref(...) pool_unref(__VA_ARGS__)
#endif

#ifdef DEBUG_POOL_REF
#define istream_free_unref(...) istream_free_unref_debug(__VA_ARGS__, const char *file, unsigned line)
#define istream_free_unref_handler(...) istream_free_unref_handler_debug(__VA_ARGS__, const char *file, unsigned line)
#endif

static inline void
istream_free_unref(istream_t *istream_r)
{
    pool_t pool = istream_pool(*istream_r);
    istream_free(istream_r);
    x_pool_unref(pool);
}

#ifdef DEBUG_POOL_REF
#undef istream_free_unref
#define istream_free_unref(...) istream_free_unref_debug(__VA_ARGS__, file, line)
#endif

static inline void
istream_free_unref_handler(istream_t *istream_r)
{
    istream_handler_clear(*istream_r);
    istream_free_unref(istream_r);
}

#ifdef DEBUG_POOL_REF
#undef istream_free_unref
#define istream_free_unref(...) istream_free_unref_debug(__VA_ARGS__, __FILE__, __LINE__)
#undef istream_free_unref_handler
#define istream_free_unref_handler(...) istream_free_unref_handler_debug(__VA_ARGS__, __FILE__, __LINE__)
#endif


#ifdef DEBUG_POOL_REF
#define istream_assign_ref(...) istream_assign_ref_debug(__VA_ARGS__, const char *file, unsigned line)
#endif

static inline void
istream_assign_ref(istream_t *istream_r, istream_t _istream)
{
    struct istream *istream = _istream_opaque_cast(_istream);

    *istream_r = _istream;
    x_pool_ref(istream->pool);
}


#ifdef DEBUG_POOL_REF
#undef istream_assign_ref
#define istream_assign_ref(...) istream_assign_ref_debug(__VA_ARGS__, file, line)
#define istream_assign_ref_handler(...) istream_assign_ref_handler_debug(__VA_ARGS__, const char *file, unsigned line)
#endif

static inline void
istream_assign_ref_handler(istream_t *istream_r, istream_t istream,
                           const struct istream_handler *handler,
                           void *handler_ctx,
                           istream_direct_t handler_direct)
{
    istream_assign_ref(istream_r, istream);
    istream_handler_set(istream, handler, handler_ctx, handler_direct);
}


#ifdef DEBUG_POOL_REF
#define istream_clear_unref(...) istream_clear_unref_debug(__VA_ARGS__, const char *file, unsigned line)
#define istream_clear_unref_handler(...) istream_clear_unref_handler_debug(__VA_ARGS__, const char *file, unsigned line)
#endif

static inline void
istream_clear_unref(istream_t *istream_r)
{
    struct istream *istream = _istream_opaque_cast(*istream_r);
    *istream_r = NULL;
    x_pool_unref(istream->pool);
}

static inline void
istream_clear_unref_handler(istream_t *istream_r)
{
    struct istream *istream = _istream_opaque_cast(*istream_r);
    *istream_r = NULL;
    istream_handler_clear(istream_struct_cast(istream));
    x_pool_unref(istream->pool);
}


#ifdef DEBUG_POOL_REF
#undef istream_assign_ref
#define istream_assign_ref(...) istream_assign_ref_debug(__VA_ARGS__, __FILE__, __LINE__)
#undef istream_assign_ref_handler
#define istream_assign_ref_handler(...) istream_assign_ref_handler_debug(__VA_ARGS__, __FILE__, __LINE__)
#undef istream_clear_unref
#define istream_clear_unref(...) istream_clear_unref_debug(__VA_ARGS__, __FILE__, __LINE__)
#undef istream_clear_unref_handler
#define istream_clear_unref_handler(...) istream_clear_unref_handler_debug(__VA_ARGS__, __FILE__, __LINE__)
#endif

#undef x_pool_ref
#undef x_pool_unref

#endif

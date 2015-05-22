/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_unlock.hxx"
#include "istream-internal.h"
#include "cache.hxx"
#include "util/Cast.hxx"

#include <assert.h>

struct istream_unlock {
    struct istream output;

    struct istream *input;

    struct cache *cache;
    struct cache_item *item;
};


/*
 * istream handler
 *
 */

static void
unlock_input_eof(void *ctx)
{
    struct istream_unlock *unlock = (struct istream_unlock *)ctx;

    cache_item_unlock(unlock->cache, unlock->item);
    istream_deinit_eof(&unlock->output);
}

static void
unlock_input_abort(GError *error, void *ctx)
{
    struct istream_unlock *unlock = (struct istream_unlock *)ctx;

    cache_item_unlock(unlock->cache, unlock->item);
    istream_deinit_abort(&unlock->output, error);
}

static const struct istream_handler unlock_input_handler = {
    .data = istream_forward_data,
    .direct = istream_forward_direct,
    .eof = unlock_input_eof,
    .abort = unlock_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_unlock *
istream_to_unlock(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_unlock::output);
}

static off_t
istream_unlock_available(struct istream *istream, bool partial)
{
    struct istream_unlock *unlock = istream_to_unlock(istream);

    return istream_available(unlock->input, partial);
}

static void
istream_unlock_read(struct istream *istream)
{
    struct istream_unlock *unlock = istream_to_unlock(istream);

    istream_handler_set_direct(unlock->input, unlock->output.handler_direct);
    istream_read(unlock->input);
}

static void
istream_unlock_close(struct istream *istream)
{
    struct istream_unlock *unlock = istream_to_unlock(istream);

    istream_close_handler(unlock->input);
    cache_item_unlock(unlock->cache, unlock->item);
    istream_deinit(&unlock->output);
}

static const struct istream_class istream_unlock = {
    .available = istream_unlock_available,
    .read = istream_unlock_read,
    .close = istream_unlock_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_unlock_new(struct pool *pool, struct istream *input,
                   struct cache *cache, struct cache_item *item)
{
    struct istream_unlock *unlock = istream_new_macro(pool, unlock);

    assert(input != nullptr);
    assert(!istream_has_handler(input));
    assert(cache != nullptr);
    assert(item != nullptr);

    istream_assign_handler(&unlock->input, input,
                           &unlock_input_handler, unlock,
                           0);

    unlock->cache = cache;
    unlock->item = item;
    cache_item_lock(item);

    return istream_struct_cast(&unlock->output);
}

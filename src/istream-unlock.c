/*
 * An istream facade which unlocks a cache item after it has been
 * closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "cache.h"

#include <assert.h>

struct istream_unlock {
    struct istream output;

    istream_t input;

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
    struct istream_unlock *unlock = ctx;

    cache_item_unlock(unlock->cache, unlock->item);
    istream_deinit_eof(&unlock->output);
}

static void
unlock_input_abort(void *ctx)
{
    struct istream_unlock *unlock = ctx;

    cache_item_unlock(unlock->cache, unlock->item);
    istream_deinit_abort(&unlock->output);
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
istream_to_unlock(istream_t istream)
{
    return (struct istream_unlock *)(((char*)istream) - offsetof(struct istream_unlock, output));
}

static off_t
istream_unlock_available(istream_t istream, bool partial)
{
    struct istream_unlock *unlock = istream_to_unlock(istream);

    return istream_available(unlock->input, partial);
}

static void
istream_unlock_read(istream_t istream)
{
    struct istream_unlock *unlock = istream_to_unlock(istream);

    istream_handler_set_direct(unlock->input, unlock->output.handler_direct);
    istream_read(unlock->input);
}

static void
istream_unlock_close(istream_t istream)
{
    struct istream_unlock *unlock = istream_to_unlock(istream);

    istream_close_handler(unlock->input);
    cache_item_unlock(unlock->cache, unlock->item);
    istream_deinit_abort(&unlock->output);
}

static const struct istream istream_unlock = {
    .available = istream_unlock_available,
    .read = istream_unlock_read,
    .close = istream_unlock_close,
};


/*
 * constructor
 *
 */

istream_t
istream_unlock_new(pool_t pool, istream_t input,
                   struct cache *cache, struct cache_item *item)
{
    struct istream_unlock *unlock = istream_new_macro(pool, unlock);

    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(cache != NULL);
    assert(item != NULL);

    istream_assign_handler(&unlock->input, input,
                           &unlock_input_handler, unlock,
                           0);

    unlock->cache = cache;
    unlock->item = item;
    cache_item_lock(item);

    return istream_struct_cast(&unlock->output);
}

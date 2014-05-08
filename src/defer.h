/*
 * Easy deferral of function calls.  Internally, this uses an event
 * struct with a zero timeout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DEFER_H
#define BENG_DEFER_H

struct pool;
struct async_operation_ref;

typedef void (*defer_callback_t)(void *ctx);

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @param async_ref this parameter may be NULL, for simplicity (if you
 * know you'll never need to abort this callback)
 */
void
defer(struct pool *pool, defer_callback_t callback, void *ctx,
      struct async_operation_ref *async_ref);

#ifdef __cplusplus
}
#endif

#endif

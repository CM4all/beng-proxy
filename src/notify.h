/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NOTIFY_H
#define BENG_PROXY_NOTIFY_H

#include <glib.h>

struct pool;
struct notify;

typedef void (*notify_callback_t)(void *ctx);

#ifdef __cplusplus
extern "C" {
#endif

struct notify *
notify_new(struct pool *pool, notify_callback_t callback, void *ctx,
           GError **error_r);

void
notify_free(struct notify *notify);

void
notify_signal(struct notify *notify);

void
notify_enable(struct notify *notify);

void
notify_disable(struct notify *notify);

#ifdef __cplusplus
}
#endif

#endif

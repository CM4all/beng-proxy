/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NOTIFY_HXX
#define BENG_PROXY_NOTIFY_HXX

#include "glibfwd.hxx"

class Notify;

typedef void (*notify_callback_t)(void *ctx);

Notify *
notify_new(notify_callback_t callback, void *ctx,
           GError **error_r);

void
notify_free(Notify *notify);

void
notify_signal(Notify *notify);

void
notify_enable(Notify *notify);

void
notify_disable(Notify *notify);

#endif

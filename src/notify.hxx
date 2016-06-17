/*
 * Send notifications from a worker thread to the main thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NOTIFY_HXX
#define BENG_PROXY_NOTIFY_HXX

#include "util/BindMethod.hxx"

class Notify;

typedef BoundMethod<void()> NotifyCallback;

Notify *
notify_new(NotifyCallback callback);

void
notify_free(Notify *notify);

void
notify_signal(Notify *notify);

void
notify_enable(Notify *notify);

void
notify_disable(Notify *notify);

#endif

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_METHOD_DEFER_EVENT_HXX
#define BENG_PROXY_METHOD_DEFER_EVENT_HXX

#include "DeferEvent.hxx"
#include "util/BindMethod.hxx"

template<typename C>
class MethodDeferEvent final : public DeferEvent {
    typedef BoundMethod<void()> Callback;
    Callback callback;

public:
    MethodDeferEvent(EventLoop &event_loop, Callback _callback)
        :DeferEvent(event_loop), callback(_callback) {}

protected:
    /* virtual methods from class DeferEvent */
    void OnDeferred() override {
        callback();
    }
};

#endif

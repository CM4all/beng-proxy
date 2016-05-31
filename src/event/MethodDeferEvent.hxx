/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_METHOD_DEFER_EVENT_HXX
#define BENG_PROXY_METHOD_DEFER_EVENT_HXX

#include "DeferEvent.hxx"

template<typename C>
class MethodDeferEvent final : public DeferEvent {
    C &object;
    void (C::*method)();

public:
    MethodDeferEvent(EventLoop &event_loop, C &_object, void (C::*_method)())
        :DeferEvent(event_loop), object(_object), method(_method) {}

protected:
    /* virtual methods from class DeferEvent */
    void OnDeferred() override {
        (object.*method)();
    }
};

#endif

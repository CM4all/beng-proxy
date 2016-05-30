/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_METHOD_DEFER_EVENT_HXX
#define BENG_PROXY_METHOD_DEFER_EVENT_HXX

#include "LightDeferEvent.hxx"

template<typename C>
class MethodDeferEvent final : public LightDeferEvent {
    C &object;
    void (C::*method)();

public:
    MethodDeferEvent(EventLoop &event_loop, C &_object, void (C::*_method)())
        :LightDeferEvent(event_loop), object(_object), method(_method) {}

protected:
    /* virtual methods from class LightDeferEvent */
    void OnDeferred() override {
        (object.*method)();
    }
};

#endif

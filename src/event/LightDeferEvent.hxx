/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LIGHT_DEFER_EVENT_HXX
#define BENG_PROXY_LIGHT_DEFER_EVENT_HXX

#include <boost/intrusive/list.hpp>

class EventLoop;

/**
 * Defer execution until the next event loop iteration.  Use this to
 * move calls out of the current stack frame, to avoid surprising side
 * effects for callers up in the call chain.
 */
class LightDeferEvent {
    friend class EventLoop;

    typedef boost::intrusive::list_member_hook<boost::intrusive::link_mode<boost::intrusive::safe_link>> SiblingsHook;
    SiblingsHook siblings;

    EventLoop &loop;

public:
    LightDeferEvent(EventLoop &_loop):loop(_loop) {}

    LightDeferEvent(const LightDeferEvent &) = delete;
    LightDeferEvent &operator=(const LightDeferEvent &) = delete;

    EventLoop &GetEventLoop() {
        return loop;
    }

    bool IsPending() const {
        return siblings.is_linked();
    }

    void Schedule();
    void Cancel();

protected:
    virtual void OnDeferred() = 0;
};

#endif

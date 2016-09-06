/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef DEFER_EVENT_HXX
#define DEFER_EVENT_HXX

#include "util/BindMethod.hxx"

#include <boost/intrusive/list.hpp>

class EventLoop;

/**
 * Defer execution until the next event loop iteration.  Use this to
 * move calls out of the current stack frame, to avoid surprising side
 * effects for callers up in the call chain.
 */
class DeferEvent final {
    friend class EventLoop;

    typedef boost::intrusive::list_member_hook<boost::intrusive::link_mode<boost::intrusive::safe_link>> SiblingsHook;
    SiblingsHook siblings;

    EventLoop &loop;

    typedef BoundMethod<void()> Callback;
    const Callback callback;

public:
    DeferEvent(EventLoop &_loop, Callback _callback)
        :loop(_loop), callback(_callback) {}

    DeferEvent(const DeferEvent &) = delete;
    DeferEvent &operator=(const DeferEvent &) = delete;

    EventLoop &GetEventLoop() {
        return loop;
    }

    bool IsPending() const {
        return siblings.is_linked();
    }

    void Schedule();
    void Cancel();

protected:
    void OnDeferred() {
        callback();
    }
};

#endif

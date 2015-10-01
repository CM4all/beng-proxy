/*
 * C++ wrappers for the libevent callback.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EVENT_CALLBACK_HXX
#define EVENT_CALLBACK_HXX

#include <inline/compiler.h>

#include <event.h>

template<class T, void (T::*member)(evutil_socket_t fd, short events)>
struct EventCallback {
    static void Callback(evutil_socket_t fd,
                         short events,
                         void *ctx) {
        T &t = *(T *)ctx;
        (t.*member)(fd, events);
    }
};

/* need C++ N3601 to do this without macros */
#define MakeEventCallback(T, C) ::EventCallback<T, &T::C>::Callback

template<class T, void (T::*member)()>
struct SimpleEventCallback {
    static void Callback(gcc_unused evutil_socket_t fd,
                         gcc_unused short events,
                         void *ctx) {
        T &t = *(T *)ctx;
        (t.*member)();
    }
};

/* need C++ N3601 to do this without macros */
#define MakeSimpleEventCallback(T, C) SimpleEventCallback<T, &T::C>::Callback

#endif

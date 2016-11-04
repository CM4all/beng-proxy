/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_SERVER_HXX
#define TRAFO_SERVER_HXX

#include "Listener.hxx"

#include <list>

class TrafoHandler;

class TrafoServer {
    EventLoop &event_loop;
    TrafoHandler &handler;

    std::list<TrafoListener> listeners;

public:
    TrafoServer(EventLoop &_event_loop, TrafoHandler &_handler)
        :event_loop(_event_loop), handler(_handler) {}

    /**
     * Throws std::runtime_error on error.
     */
    void ListenPath(const char *path);
};

#endif

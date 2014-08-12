/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRAFO_SERVER_HXX
#define TRAFO_SERVER_HXX

#include "Listener.hxx"

#include <list>

class Error;
class TrafoHandler;

class TrafoServer {
    TrafoHandler &handler;

    std::list<TrafoListener> listeners;

public:
    TrafoServer(TrafoHandler &_handler)
        :handler(_handler) {}

    bool ListenPath(const char *path, Error &error);
};

#endif

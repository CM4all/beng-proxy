/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef AVAHI_CLIENT_HXX
#define AVAHI_CLIENT_HXX

#include "Poll.hxx"
#include "event/TimerEvent.hxx"
#include "io/Logger.hxx"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>

#include <string>
#include <forward_list>

class EventLoop;
class SocketAddress;
class AvahiConnectionListener;

class MyAvahiClient final {
    const LLogger logger;

    std::string name;

    TimerEvent reconnect_timer;

    MyAvahiPoll poll;

    AvahiClient *client = nullptr;
    AvahiEntryGroup *group = nullptr;

    struct Service {
        AvahiIfIndex interface;
        AvahiProtocol protocol;
        std::string type;
        uint16_t port;

        Service(AvahiIfIndex _interface, AvahiProtocol _protocol,
                const char *_type, uint16_t _port)
            :interface(_interface), protocol(_protocol),
             type(_type), port(_port) {}
    };

    std::forward_list<Service> services;

    std::forward_list<AvahiConnectionListener *> listeners;

    /**
     * Shall the published services be visible?  This is controlled by
     * HideServices() and ShowServices().
     */
    bool visible_services = false;

public:
    MyAvahiClient(EventLoop &event_loop, const char *_name);
    ~MyAvahiClient();

    MyAvahiClient(const MyAvahiClient &) = delete;
    MyAvahiClient &operator=(const MyAvahiClient &) = delete;

    EventLoop &GetEventLoop() {
        return poll.GetEventLoop();
    }

    void Close();

    void Activate();

    void AddListener(AvahiConnectionListener &listener) {
        listeners.push_front(&listener);
    }

    void RemoveListener(AvahiConnectionListener &listener) {
        listeners.remove(&listener);
    }

    void AddService(AvahiIfIndex interface, AvahiProtocol protocol,
                    const char *type, uint16_t port);
    void AddService(const char *type, const char *interface,
                    SocketAddress address);

    /**
     * Temporarily hide all registered services.  You can undo this
     * with ShowServices().
     */
    void HideServices();

    /**
     * Undo HideServices().
     */
    void ShowServices();

private:
    void GroupCallback(AvahiEntryGroup *g, AvahiEntryGroupState state);
    static void GroupCallback(AvahiEntryGroup *g,
                              AvahiEntryGroupState state,
                              void *userdata);

    void RegisterServices(AvahiClient *c);

    void ClientCallback(AvahiClient *c, AvahiClientState state);
    static void ClientCallback(AvahiClient *c, AvahiClientState state,
                               void *userdata);

    void OnReconnectTimer();
};

#endif

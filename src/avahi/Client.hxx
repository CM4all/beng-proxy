/*
 * author: Max Kellermann <mk@cm4all.com>
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

public:
    MyAvahiClient(EventLoop &event_loop, const char *_name);
    ~MyAvahiClient();

    MyAvahiClient(const MyAvahiClient &) = delete;
    MyAvahiClient &operator=(const MyAvahiClient &) = delete;

    EventLoop &GetEventLoop() {
        return poll.GetEventLoop();
    }

    void Close();

    void Enable();

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

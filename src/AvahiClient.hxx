/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef AVAHI_CLIENT_HXX
#define AVAHI_CLIENT_HXX

#include "event/TimerEvent.hxx"
#include "AvahiPoll.hxx"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>

#include <string>
#include <forward_list>

class EventLoop;
class SocketAddress;

class MyAvahiClient final {
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

public:
    MyAvahiClient(EventLoop &event_loop, const char *_name);
    ~MyAvahiClient();

    MyAvahiClient(const MyAvahiClient &) = delete;
    MyAvahiClient &operator=(const MyAvahiClient &) = delete;

    void AddService(AvahiIfIndex interface, AvahiProtocol protocol,
                    const char *type, uint16_t port);
    void AddService(const char *type, SocketAddress address);

private:
    void Close();

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

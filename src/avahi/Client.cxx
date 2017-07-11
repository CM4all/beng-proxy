/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Client.hxx"
#include "ConnectionListener.hxx"
#include "event/Duration.hxx"
#include "net/SocketAddress.hxx"
#include "net/Interface.hxx"

#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/alternative.h>

#include <stdio.h>
#include <unistd.h>
#include <net/if.h>

/**
 * Append the process id to the given prefix string.  This is used as
 * a workaround for an avahi-daemon bug/problem: when a service gets
 * restarted, and then binds to a new port number (e.g. beng-proxy
 * with automatic port assignment), we don't get notified, and so we
 * never query the new port.  By appending the process id to the
 * client name, we ensure that the exiting old process broadcasts
 * AVAHI_BROWSER_REMOVE, and hte new process broadcasts
 * AVAHI_BROWSER_NEW.
 */
static std::string
MakePidName(const char *prefix)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s[%u]", prefix, (unsigned)getpid());
    return buffer;
}

MyAvahiClient::MyAvahiClient(EventLoop &event_loop, const char *_name)
    :logger("avahi"), name(MakePidName(_name)),
     reconnect_timer(event_loop, BIND_THIS_METHOD(OnReconnectTimer)),
     poll(event_loop)
{
}

MyAvahiClient::~MyAvahiClient()
{
    Close();
}

void
MyAvahiClient::Enable()
{
    assert(client == nullptr);

    reconnect_timer.Add(EventDuration<0, 1000>::value);
}

void
MyAvahiClient::AddService(AvahiIfIndex interface, AvahiProtocol protocol,
                          const char *type, uint16_t port)
{
    /* cannot register any more services after initial connect */
    assert(client == nullptr);

    services.emplace_front(interface, protocol, type, port);

    Enable();
}

void
MyAvahiClient::AddService(const char *type, const char *interface,
                          SocketAddress address)
{
    unsigned port = address.GetPort();
    if (port == 0)
        return;

    unsigned i = 0;
    if (interface != nullptr)
        i = if_nametoindex(interface);

    if (i == 0)
        i = FindNetworkInterface(address);

    AvahiIfIndex ii = i > 0
        ? AvahiIfIndex(i)
        : AVAHI_IF_UNSPEC;

    AvahiProtocol protocol = AVAHI_PROTO_UNSPEC;
    switch (address.GetFamily()) {
    case AF_INET:
        protocol = AVAHI_PROTO_INET;
        break;

    case AF_INET6:
        protocol = AVAHI_PROTO_INET6;
        break;
    }

    AddService(ii, protocol, type, port);
}

void
MyAvahiClient::Close()
{
    if (group != nullptr) {
        avahi_entry_group_free(group);
        group = nullptr;
    }

    if (client != nullptr) {
        for (auto *l : listeners)
            l->OnAvahiDisconnect();

        avahi_client_free(client);
        client = nullptr;
    }

    reconnect_timer.Cancel();
}

void
MyAvahiClient::GroupCallback(AvahiEntryGroup *g, AvahiEntryGroupState state)
{
    switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
        break;

    case AVAHI_ENTRY_GROUP_COLLISION:
        /* pick a new name */

        {
            char *new_name = avahi_alternative_service_name(name.c_str());
            name = new_name;
            avahi_free(new_name);
        }

        /* And recreate the services */
        RegisterServices(avahi_entry_group_get_client(g));
        break;

    case AVAHI_ENTRY_GROUP_FAILURE:
        logger(3, "Avahi service group failure: ",
               avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
        break;

    case AVAHI_ENTRY_GROUP_UNCOMMITED:
    case AVAHI_ENTRY_GROUP_REGISTERING:
        break;
    }
}

void
MyAvahiClient::GroupCallback(AvahiEntryGroup *g,
                             AvahiEntryGroupState state,
                             void *userdata)
{
    auto &client = *(MyAvahiClient *)userdata;
    client.GroupCallback(g, state);
}

void
MyAvahiClient::RegisterServices(AvahiClient *c)
{
    if (group == nullptr) {
        group = avahi_entry_group_new(c, GroupCallback, this);
        if (group == nullptr) {
            logger(3, "Failed to create Avahi service group: ",
                   avahi_strerror(avahi_client_errno(c)));
            return;
        }
    }

    for (const auto &i : services) {
        int error = avahi_entry_group_add_service(group,
                                                  i.interface,
                                                  i.protocol,
                                                  AvahiPublishFlags(0),
                                                  name.c_str(), i.type.c_str(),
                                                  nullptr, nullptr,
                                                  i.port, nullptr);
        if (error < 0) {
            logger(3, "Failed to add Avahi service ", i.type.c_str(),
                   ": ", avahi_strerror(error));
            return;
        }
    }

    int result = avahi_entry_group_commit(group);
    if (result < 0) {
        logger(3, "Failed to commit Avahi service group: ",
               avahi_strerror(result));
        return;
    }
}

void
MyAvahiClient::ClientCallback(AvahiClient *c, AvahiClientState state)
{
    int error;

    switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
        if (!services.empty() && group == nullptr)
            RegisterServices(c);

        for (auto *l : listeners)
            l->OnAvahiConnect(c);

        break;

    case AVAHI_CLIENT_FAILURE:
        error = avahi_client_errno(c);
        if (error == AVAHI_ERR_DISCONNECTED) {
            Close();

            reconnect_timer.Add(EventDuration<10, 0>::value);
        } else {
            logger(3, "Avahi client failed: ", avahi_strerror(error));
            reconnect_timer.Add(EventDuration<60, 0>::value);
        }

        for (auto *l : listeners)
            l->OnAvahiDisconnect();

        break;

    case AVAHI_CLIENT_S_COLLISION:
    case AVAHI_CLIENT_S_REGISTERING:
        if (group != nullptr)
            avahi_entry_group_reset(group);

        break;

    case AVAHI_CLIENT_CONNECTING:
        break;
    }
}

void
MyAvahiClient::ClientCallback(AvahiClient *c, AvahiClientState state,
                              void *userdata)
{
    auto &client = *(MyAvahiClient *)userdata;
    client.ClientCallback(c, state);
}

void
MyAvahiClient::OnReconnectTimer()
{
    int error;
    client = avahi_client_new(&poll, AVAHI_CLIENT_NO_FAIL,
                              ClientCallback, this,
                              &error);
    if (client == nullptr) {
        logger(3, "Failed to create avahi client: ",
               avahi_strerror(error));
        reconnect_timer.Add(EventDuration<60, 0>::value);
        return;
    }
}

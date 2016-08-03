/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AvahiClient.hxx"
#include "event/Duration.hxx"
#include "net/SocketAddress.hxx"
#include "net/Interface.hxx"

#include <daemon/log.h>

#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/alternative.h>

MyAvahiClient::MyAvahiClient(EventLoop &event_loop, const char *_name)
    :name(_name),
     reconnect_timer(event_loop, BIND_THIS_METHOD(OnReconnectTimer)),
     poll(event_loop)
{
}

MyAvahiClient::~MyAvahiClient()
{
    Close();
}

void
MyAvahiClient::AddService(AvahiIfIndex interface, AvahiProtocol protocol,
                          const char *type, uint16_t port)
{
    /* cannot register any more services after initial connect */
    assert(client == nullptr);

    if (services.empty())
        /* initiate the connection */
        reconnect_timer.Add(EventDuration<0, 10000>::value);

    services.emplace_front(interface, protocol, type, port);
}

void
MyAvahiClient::AddService(const char *type, SocketAddress address)
{
    unsigned port = address.GetPort();
    if (port == 0)
        return;

    unsigned i = FindNetworkInterface(address);
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

    if (client != nullptr)
        avahi_client_free(client);
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
        daemon_log(3, "Avahi service group failure: %s",
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
            daemon_log(3, "Failed to create Avahi service group: %s",
                       avahi_strerror(avahi_client_errno(c)));
            return;
        }
    }

    for (const auto &i : services) {
        int error = avahi_entry_group_add_service(group,
                                                  AVAHI_IF_UNSPEC,
                                                  AVAHI_PROTO_UNSPEC,
                                                  AvahiPublishFlags(0),
                                                  name.c_str(), i.type.c_str(),
                                                  nullptr, nullptr,
                                                  i.port, nullptr);
        if (error < 0) {
            daemon_log(3, "Failed to add Avahi service %s: %s",
                       i.type.c_str(), avahi_strerror(error));
            return;
        }
    }

    int result = avahi_entry_group_commit(group);
    if (result < 0) {
        daemon_log(3, "Failed to commit Avahi service group: %s",
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
        if (group == nullptr)
            RegisterServices(c);
        break;

    case AVAHI_CLIENT_FAILURE:
        error = avahi_client_errno(c);
        if (error == AVAHI_ERR_DISCONNECTED) {
            Close();

            reconnect_timer.Add(EventDuration<10, 0>::value);
        } else {
            daemon_log(3, "Avahi client failed: %s", avahi_strerror(error));
            reconnect_timer.Add(EventDuration<60, 0>::value);
        }

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
        daemon_log(3, "Failed to create avahi client: %s",
                   avahi_strerror(error));
        reconnect_timer.Add(EventDuration<60, 0>::value);
        return;
    }
}

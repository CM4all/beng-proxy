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

#include "Explorer.hxx"
#include "ExplorerListener.hxx"
#include "Client.hxx"
#include "net/IPv4Address.hxx"
#include "net/IPv6Address.hxx"
#include "util/Cast.hxx"

#include <avahi-common/error.h>

AvahiServiceExplorer::Object::~Object()
{
    if (resolver != nullptr)
        avahi_service_resolver_free(resolver);
}

inline const std::string &
AvahiServiceExplorer::Object::GetKey() const
{
    /* this is a kludge which takes advantage of the fact that all
       instances of this class are inside std::map */
    auto &p = ContainerCast(*this, &Map::value_type::second);
    return p.first;
}

void
AvahiServiceExplorer::Object::Resolve(AvahiClient *client, AvahiIfIndex interface,
                                      AvahiProtocol protocol,
                                      const char *name,
                                      const char *type,
                                      const char *domain)
{
    assert(resolver == nullptr);

    resolver = avahi_service_resolver_new(client, interface, protocol,
                                          name, type, domain,
                                          /* workaround: the following
                                             should be
                                             AVAHI_PROTO_UNSPEC
                                             (because we can deal with
                                             either protocol), but
                                             then avahi-daemon
                                             sometimes returns IPv6
                                             addresses from the cache,
                                             even though the service
                                             was registered as IPv4
                                             only */
                                          protocol,
                                          AvahiLookupFlags(0),
                                          ServiceResolverCallback, this);
    if (resolver == nullptr)
        explorer.logger(2, "Failed to create Avahi service resolver: ",
                        avahi_strerror(avahi_client_errno(client)));
}

void
AvahiServiceExplorer::Object::CancelResolve()
{
    if (resolver != nullptr) {
        avahi_service_resolver_free(resolver);
        resolver = nullptr;
    }
}

static AllocatedSocketAddress
Import(const AvahiIPv4Address &src, unsigned port)
{
    return AllocatedSocketAddress(IPv4Address({src.address}, port));
}

static AllocatedSocketAddress
Import(AvahiIfIndex interface, const AvahiIPv6Address &src, unsigned port)
{
    struct in6_addr address;
    static_assert(sizeof(src.address) == sizeof(address), "Wrong size");
    std::copy_n(src.address, sizeof(src.address), address.s6_addr);
    return AllocatedSocketAddress(IPv6Address(address, port, interface));
}

static AllocatedSocketAddress
Import(AvahiIfIndex interface, const AvahiAddress &src, unsigned port)
{
    switch (src.proto) {
    case AVAHI_PROTO_INET:
        return Import(src.data.ipv4, port);

    case AVAHI_PROTO_INET6:
        return Import(interface, src.data.ipv6, port);
    }

    return AllocatedSocketAddress();
}

void
AvahiServiceExplorer::Object::ServiceResolverCallback(AvahiIfIndex interface,
                                                      AvahiResolverEvent event,
                                                      const AvahiAddress *a,
                                                      uint16_t port)
{
    switch (event) {
    case AVAHI_RESOLVER_FOUND:
        address = Import(interface, *a, port);
        explorer.listener.OnAvahiNewObject(GetKey(), address);
        break;

    case AVAHI_RESOLVER_FAILURE:
        break;
    }

    CancelResolve();
}

void
AvahiServiceExplorer::Object::ServiceResolverCallback(AvahiServiceResolver *,
                                                      AvahiIfIndex interface,
                                                      gcc_unused AvahiProtocol protocol,
                                                      AvahiResolverEvent event,
                                                      gcc_unused const char *name,
                                                      gcc_unused const char *type,
                                                      gcc_unused const char *domain,
                                                      gcc_unused const char *host_name,
                                                      const AvahiAddress *a,
                                                      uint16_t port,
                                                      gcc_unused AvahiStringList *txt,
                                                      gcc_unused AvahiLookupResultFlags flags,
                                                      void *userdata)
{
    auto &object = *(AvahiServiceExplorer::Object *)userdata;
    object.ServiceResolverCallback(interface, event, a, port);
}

AvahiServiceExplorer::AvahiServiceExplorer(MyAvahiClient &_avahi_client,
                                           AvahiServiceExplorerListener &_listener,
                                           AvahiIfIndex _interface,
                                           AvahiProtocol _protocol,
                                           const char *_type,
                                           const char *_domain)
    :logger("avahi"),
     avahi_client(_avahi_client),
     listener(_listener),
     query_interface(_interface), query_protocol(_protocol),
     query_type(_type == nullptr ? "" : _type),
     query_domain(_domain == nullptr ? "" : _domain)
{
    avahi_client.AddListener(*this);
    avahi_client.Enable();
}

AvahiServiceExplorer::~AvahiServiceExplorer()
{
    if (avahi_browser != nullptr)
        avahi_service_browser_free(avahi_browser);

    avahi_client.RemoveListener(*this);
}

static std::string
MakeKey(AvahiIfIndex interface,
        AvahiProtocol protocol,
        const char *name,
        const char *type,
        const char *domain)
{
    char buffer[2048];
    snprintf(buffer, sizeof(buffer), "%d/%d/%s/%s/%s",
             (int)interface, (int)protocol, name, type, domain);
    return buffer;
}

void
AvahiServiceExplorer::ServiceBrowserCallback(AvahiServiceBrowser *b,
                                             AvahiIfIndex interface,
                                             AvahiProtocol protocol,
                                             AvahiBrowserEvent event,
                                             const char *name,
                                             const char *type,
                                             const char *domain,
                                             gcc_unused AvahiLookupResultFlags flags)
{
    if (event == AVAHI_BROWSER_NEW) {
        auto i = objects.emplace(std::piecewise_construct,
                                 std::forward_as_tuple(MakeKey(interface,
                                                               protocol, name,
                                                               type, domain)),
                                 std::forward_as_tuple(*this));
        if (i.second || i.first->second.HasFailed())
            i.first->second.Resolve(avahi_service_browser_get_client(b),
                                    interface, protocol,
                                    name, type, domain);
    } else if (event == AVAHI_BROWSER_REMOVE) {
        auto i = objects.find(MakeKey(interface, protocol, name,
                                      type, domain));
        if (i != objects.end()) {
            if (i->second.IsActive())
                listener.OnAvahiRemoveObject(i->first.c_str());

            objects.erase(i);
        }
    }
}

void
AvahiServiceExplorer::ServiceBrowserCallback(AvahiServiceBrowser *b,
                                             AvahiIfIndex interface,
                                             AvahiProtocol protocol,
                                             AvahiBrowserEvent event,
                                             const char *name,
                                             const char *type,
                                             const char *domain,
                                             AvahiLookupResultFlags flags,
                                             void *userdata)
{
    auto &cluster = *(AvahiServiceExplorer *)userdata;
    cluster.ServiceBrowserCallback(b, interface, protocol, event, name,
                                   type, domain, flags);
}

void
AvahiServiceExplorer::OnAvahiConnect(AvahiClient *client)
{
    avahi_browser = avahi_service_browser_new(client,
                                              query_interface, query_protocol,
                                              query_type.c_str(),
                                              query_domain.empty() ? nullptr : query_domain.c_str(),
                                              AvahiLookupFlags(0),
                                              ServiceBrowserCallback, this);
    if (avahi_browser == nullptr)
        logger(2, "Failed to create Avahi service browser: ",
               avahi_strerror(avahi_client_errno(client)));
}

void
AvahiServiceExplorer::OnAvahiDisconnect()
{
    for (auto &i : objects)
        i.second.CancelResolve();

    if (avahi_browser != nullptr) {
        avahi_service_browser_free(avahi_browser);
        avahi_browser = nullptr;
    }
}

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef AVAHI_EXPLORER_LISTENER_HXX
#define AVAHI_EXPLORER_LISTENER_HXX

#include <string>

class SocketAddress;

class AvahiServiceExplorerListener {
public:
    virtual void OnAvahiNewObject(const std::string &key,
                                  SocketAddress address) = 0;
    virtual void OnAvahiRemoveObject(const std::string &key) = 0;
};

#endif

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef AVAHI_CONNECTION_LISTENER_HXX
#define AVAHI_CONNECTION_LISTENER_HXX

#include <avahi-client/client.h>

class AvahiConnectionListener {
public:
    virtual void OnAvahiConnect(AvahiClient *client) = 0;
    virtual void OnAvahiDisconnect() = 0;
};

#endif

/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_config.hxx"

#include <string.h>
#include <netdb.h>

ListenerConfig::~ListenerConfig()
{
    if (address != nullptr)
        freeaddrinfo(address);
}

BpConfig::BpConfig()
{
    memset(&user, 0, sizeof(user));
}

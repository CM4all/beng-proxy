/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GotoMap.hxx"
#include "lb/LuaInitHook.hxx"
#include "avahi/Client.hxx"

void
LbGotoMap::Scan(const LbConfig &config, MyAvahiClient &avahi_client)
{
    clusters.Scan(config, avahi_client);

    {
        LbLuaInitHook init_hook(config, &clusters, &avahi_client);
        lua_handlers.Scan(init_hook, config);
    }

    translation_handlers.Scan(config, avahi_client.GetEventLoop());
}

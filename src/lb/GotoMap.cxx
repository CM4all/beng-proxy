/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "GotoMap.hxx"
#include "Goto.hxx"
#include "GotoConfig.hxx"
#include "lb/LuaInitHook.hxx"
#include "lb_config.hxx"
#include "avahi/Client.hxx"

void
LbGotoMap::FlushCaches()
{
    for (auto &i : translation_handlers)
        i.second.FlushCache();
}

void
LbGotoMap::InvalidateTranslationCaches(const TranslateRequest &request)
{
    for (auto &i : translation_handlers)
        i.second.InvalidateCache(request);
}

LbGoto
LbGotoMap::GetInstance(const char *name)
{
    return GetInstance(root_config.FindGoto(name));
}

LbGoto
LbGotoMap::GetInstance(const LbGotoConfig &config)
{
    if (config.cluster != nullptr)
        return GetInstance(*config.cluster);
    else if (config.branch != nullptr)
        return GetInstance(*config.branch);
    else if (config.lua != nullptr)
        return GetInstance(*config.lua);
    else if (config.translation != nullptr)
        return GetInstance(*config.translation);
    else if (config.response.IsDefined())
        return config.response;
    else
        return LbGoto();
}

LbCluster &
LbGotoMap::GetInstance(const LbClusterConfig &config)
{
    return clusters.emplace(std::piecewise_construct,
                            std::forward_as_tuple(&config),
                            std::forward_as_tuple(config, avahi_client))
        .first->second;
}

LbBranch &
LbGotoMap::GetInstance(const LbBranchConfig &config)
{
    return branches.emplace(std::piecewise_construct,
                            std::forward_as_tuple(&config),
                            std::forward_as_tuple(*this, config))
        .first->second;
}

LbLuaHandler &
LbGotoMap::GetInstance(const LbLuaHandlerConfig &config)
{
    return lua_handlers.emplace(std::piecewise_construct,
                                std::forward_as_tuple(&config),
                                std::forward_as_tuple(lua_init_hook, config))
        .first->second;
}

LbTranslationHandler &
LbGotoMap::GetInstance(const LbTranslationHandlerConfig &config)
{
    return translation_handlers.emplace(std::piecewise_construct,
                                        std::forward_as_tuple(&config),
                                        std::forward_as_tuple(avahi_client.GetEventLoop(),
                                                              *this, config))
        .first->second;
}

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "TranslationHandlerMap.hxx"
#include "lb_config.hxx"

void
LbTranslationHandlerMap::Scan(const LbConfig &config, EventLoop &event_loop)
{
    for (const auto &i : config.translation_handlers)
        Scan(i.second, event_loop);
}

void
LbTranslationHandlerMap::Scan(const LbTranslationHandlerConfig &config,
                              EventLoop &event_loop)
{
    handlers.emplace(std::piecewise_construct,
                     std::forward_as_tuple(config.name.c_str()),
                     std::forward_as_tuple(event_loop, config));
}

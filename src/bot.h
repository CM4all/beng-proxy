/*
 * Identify well-known bots and crawlers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BOT_H
#define BENG_PROXY_BOT_H

#include <stdbool.h>

/**
 * Checks if the specified User-Agent request header is a well-known
 * bot.  This is notoriously unreliable, because we cannot know all
 * bots in the world.  This is just here to optimize session
 * management: don't create sessions for bots.
 */
bool
user_agent_is_bot(const char *user_agent);

#endif

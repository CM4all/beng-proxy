/*
 * Identify well-known bots and crawlers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bot.h"

#include <assert.h>
#include <string.h>

bool
user_agent_is_bot(const char *user_agent)
{
    assert(user_agent != NULL);

    return strstr(user_agent, "bot.html") != NULL || /* Google and MSN */
        strstr(user_agent, "ysearch") != NULL || /* Yahoo */
        false;
}

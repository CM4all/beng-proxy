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

    return strstr(user_agent, "bot.htm") != NULL || /* Google and MSN/Bing */
        strstr(user_agent, "bot@") != NULL || /* Ezooms */
        strstr(user_agent, "ysearch") != NULL || /* Yahoo */
        strstr(user_agent, "bot.php") != NULL || /* Majestic */
        strstr(user_agent, "webspider.htm") != NULL || /* Sosospider */
        strstr(user_agent, "+crawler") != NULL || /* Pixray-Seeker */
        strstr(user_agent, "crawler@") != NULL || /* Alexa */
        strstr(user_agent, "crawlerinfo.html") != NULL || /* TurnitinBot */
        strstr(user_agent, "/robot") != NULL || /* AhrefsBot and Exabot */
        strstr(user_agent, "/bots") != NULL || /* Yandex */
        strstr(user_agent, "/crawler.") != NULL || /* Sistrix */
        false;
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Bot.hxx"

#include <assert.h>
#include <string.h>

bool
user_agent_is_bot(const char *user_agent) noexcept
{
	assert(user_agent != nullptr);

	return strstr(user_agent, "bot.htm") != nullptr || /* Google and MSN/Bing */
		strstr(user_agent, "bot@") != nullptr || /* Ezooms */
		strstr(user_agent, "ysearch") != nullptr || /* Yahoo */
		strstr(user_agent, "bot.php") != nullptr || /* Majestic */
		strstr(user_agent, "MJ12bot/") != nullptr || /* Majestic */
		strstr(user_agent, "webspider.htm") != nullptr || /* Sosospider */
		strstr(user_agent, "+crawler") != nullptr || /* Pixray-Seeker */
		strstr(user_agent, "crawler@") != nullptr || /* Alexa */
		strstr(user_agent, "crawlerinfo.html") != nullptr || /* TurnitinBot */
		strstr(user_agent, "/robot") != nullptr || /* AhrefsBot and Exabot */
		strstr(user_agent, "/bots") != nullptr || /* Yandex */
		strstr(user_agent, "/crawler.") != nullptr || /* Sistrix */
		strstr(user_agent, "Applebot") != nullptr ||
		strstr(user_agent, "WordPress/") != nullptr || /* WordPress pingbacks */
		strstr(user_agent, "pingback") != nullptr || /* WordPress (and other?) pingbacks */
		strstr(user_agent, "adscanner") != nullptr || /* http://seocompany.store */
		strstr(user_agent, "DotBot") != nullptr || /* http://www.opensiteexplorer.org/dotbot */
		strstr(user_agent, "serpstatbot") != nullptr || /* http://serpstatbot.com/ */
		strstr(user_agent, "AspiegelBot") != nullptr || /* Huawei */
		false;
}

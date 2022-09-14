/*
 * Copyright 2007-2022 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

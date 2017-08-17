/*
 * Copyright 2007-2017 Content Management AG
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
        strstr(user_agent, "MJ12bot/") != NULL || /* Majestic */
        strstr(user_agent, "webspider.htm") != NULL || /* Sosospider */
        strstr(user_agent, "+crawler") != NULL || /* Pixray-Seeker */
        strstr(user_agent, "crawler@") != NULL || /* Alexa */
        strstr(user_agent, "crawlerinfo.html") != NULL || /* TurnitinBot */
        strstr(user_agent, "/robot") != NULL || /* AhrefsBot and Exabot */
        strstr(user_agent, "/bots") != NULL || /* Yandex */
        strstr(user_agent, "/crawler.") != NULL || /* Sistrix */
        strstr(user_agent, "WordPress/") != NULL || /* WordPress pingbacks */
        strstr(user_agent, "pingback") != NULL || /* WordPress (and other?) pingbacks */
        false;
}

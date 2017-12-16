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

#include "cookie_client.hxx"
#include "cookie_jar.hxx"
#include "cookie_string.hxx"
#include "strmap.hxx"
#include "http_string.hxx"
#include "tpool.hxx"
#include "pool.hxx"
#include "shm/dpool.hxx"
#include "util/StringView.hxx"

#include <iterator>

#include <stdlib.h>
#include <string.h>

static bool
domain_matches(const char *domain, const char *match)
{
    size_t domain_length = strlen(domain);
    size_t match_length = strlen(match);

    return domain_length >= match_length &&
        strcasecmp(domain + domain_length - match_length, match) == 0 &&
        (domain_length == match_length || /* "a.b" matches "a.b" */
         match[0] == '.' || /* "a.b" matches ".b" */
         /* "a.b" matches "b" (implicit dot according to RFC 2965
            3.2.2): */
         (domain_length > match_length &&
          domain[domain_length - match_length - 1] == '.'));
}

static bool
path_matches(const char *path, const char *match)
{
    return match == nullptr || memcmp(path, match, strlen(match)) == 0;
}

template<typename L>
static void
cookie_list_delete_match(struct dpool &dpool, L &list,
                         const char *domain, const char *path,
                         StringView name)
{
    assert(domain != nullptr);

    list.remove_and_dispose_if([=](const Cookie &cookie){
            return domain_matches(domain, cookie.domain) &&
                (cookie.path == nullptr
                 ? path == nullptr
                 : path_matches(cookie.path, path)) &&
                cookie.name.Equals(name);
        },
        Cookie::Disposer(dpool));
}

static Cookie *
parse_next_cookie(struct dpool &pool, StringView &input)
{
    StringView name, value;
    cookie_next_name_value(*tpool, input, name, value, false);
    if (name.empty())
        return nullptr;

    auto *cookie = NewFromPool<Cookie>(pool, pool, name, value);

    input.StripLeft();
    while (!input.empty() && input.front() == ';') {
        input.pop_front();

        http_next_name_value(*tpool, input, name, value);
        if (name.EqualsIgnoreCase("domain"))
            cookie->domain = d_strdup(pool, value);
        else if (name.EqualsIgnoreCase("path"))
            cookie->path = d_strdup(pool, value);
        else if (name.EqualsIgnoreCase("max-age")) {
            unsigned long seconds;
            char *endptr;

            seconds = strtoul(p_strdup(*tpool, value), &endptr, 10);
            if (*endptr == 0) {
                if (seconds == 0)
                    cookie->expires = Expiry::AlreadyExpired();
                else
                    cookie->expires.Touch(std::chrono::seconds(seconds));
            }
        }

        input.StripLeft();
    }

    return cookie;
}

static bool
apply_next_cookie(CookieJar &jar, StringView &input,
                  const char *domain, const char *path)
{
    assert(domain != nullptr);

    auto *cookie = parse_next_cookie(jar.pool, input);
    if (cookie == nullptr)
        return false;

    if (cookie->domain == nullptr) {
        cookie->domain = d_strdup(jar.pool, domain);
    } else if (!domain_matches(domain, cookie->domain)) {
        /* discard if domain mismatch */
        cookie->Free(jar.pool);
        return false;
    }

    if (path != nullptr && cookie->path != nullptr &&
        !path_matches(path, cookie->path)) {
        /* discard if path mismatch */
        cookie->Free(jar.pool);
        return false;
    }

    /* delete the old cookie */
    cookie_list_delete_match(jar.pool, jar.cookies, cookie->domain,
                             cookie->path,
                             cookie->name);

    /* add the new one */

    if (cookie->expires == Expiry::AlreadyExpired())
        /* discard expired cookie */
        cookie->Free(jar.pool);
    else
        jar.Add(*cookie);

    return true;
}

void
cookie_jar_set_cookie2(CookieJar &jar, const char *value,
                       const char *domain, const char *path)
try {
    const AutoRewindPool auto_rewind(*tpool);

    StringView input = value;
    while (1) {
        if (!apply_next_cookie(jar, input, domain, path))
            break;

        if (input.empty())
            return;

        if (input.front() != ',')
            break;

        input.pop_front();
        input.StripLeft();
    }

} catch (const std::bad_alloc &) {
    /* XXX log error */
}

char *
cookie_jar_http_header_value(const CookieJar &jar,
                             const char *domain, const char *path,
                             struct pool &pool)
{
    static constexpr size_t buffer_size = 4096;

    assert(domain != nullptr);
    assert(path != nullptr);

    if (jar.cookies.empty())
        return nullptr;

    const AutoRewindPool auto_rewind(*tpool);

    char *buffer = (char *)p_malloc(tpool, buffer_size);

    size_t length = 0;

    for (auto i = jar.cookies.begin(), end = jar.cookies.end(), next = i;
         i != end; i = next) {
        next = std::next(i);

        auto *const cookie = &*i;

        if (!domain_matches(domain, cookie->domain) ||
            !path_matches(path, cookie->path))
            continue;

        if (buffer_size - length < cookie->name.size + 1 + 1 + cookie->value.size * 2 + 1 + 2)
            break;

        if (length > 0) {
            buffer[length++] = ';';
            buffer[length++] = ' ';
        }

        memcpy(buffer + length, cookie->name.data, cookie->name.size);
        length += cookie->name.size;
        buffer[length++] = '=';
        if (http_must_quote_token(cookie->value))
            length += http_quote_string(buffer + length, cookie->value);
        else {
            memcpy(buffer + length, cookie->value.data, cookie->value.size);
            length += cookie->value.size;
        }
    }

    char *value;
    if (length > 0)
        value = p_strndup(&pool, buffer, length);
    else
        value = nullptr;

    return value;
}

void
cookie_jar_http_header(const CookieJar &jar,
                       const char *domain, const char *path,
                       StringMap &headers, struct pool &pool)
{
    char *cookie = cookie_jar_http_header_value(jar, domain, path, pool);

    if (cookie != nullptr) {
        headers.Add("cookie2", "$Version=\"1\"");
        headers.Add("cookie", cookie);
    }
}

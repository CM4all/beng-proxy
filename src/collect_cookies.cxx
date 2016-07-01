/*
 * Serve HTTP requests from another HTTP/AJP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "strmap.hxx"
#include "cookie_client.hxx"

inline const char *
Request::GetCookieHost() const
{
    if (translate.response->cookie_host != nullptr)
        return translate.response->cookie_host;

    return translate.address.GetHostAndPort();
}

void
Request::CollectCookies(const StringMap &headers)
{
    auto r = headers.EqualRange("set-cookie2");
    if (r.first == r.second) {
        r = headers.EqualRange("set-cookie");
        if (r.first == r.second)
            return;
    }

    const char *host_and_port = GetCookieHost();
    if (host_and_port == nullptr)
        return;

    const char *path = GetCookieURI();
    if (path == nullptr)
        return;

    auto session = MakeRealmSession();
    if (!session)
        return;

    for (auto i = r.first; i != r.second; ++i)
        cookie_jar_set_cookie2(session->cookies, i->value,
                               host_and_port, path);
}

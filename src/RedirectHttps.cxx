/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "RedirectHttps.hxx"
#include "pool.hxx"
#include "net/HostParser.hxx"

#include <stdio.h>

const char *
MakeHttpsRedirect(struct pool &p, const char *_host, uint16_t port,
                  const char *uri)
{
    char port_buffer[16];
    size_t port_length = 0;
    if (port != 0 && port != 443)
        port_length = sprintf(port_buffer, ":%u", port);

    auto eh = ExtractHost(_host);
    auto host = !eh.host.IsNull()
        ? eh.host
        : _host;

    static constexpr char a = '[';
    static constexpr char b = ']';
    const size_t is_ipv6 = !eh.host.IsNull() && eh.host.Find(':') != nullptr;
    const size_t need_brackets = is_ipv6 && port_length > 0;

    return p_strncat(&p, "https://", size_t(8),
                     &a, need_brackets,
                     host.data, host.size,
                     &b, need_brackets,
                     port_buffer, port_length,
                     uri, strlen(uri),
                     nullptr);
}

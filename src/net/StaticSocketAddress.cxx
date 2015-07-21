/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "StaticSocketAddress.hxx"
#include "Error.hxx"
#include "util/Error.hxx"

#include <socket/resolver.h>

#include <assert.h>
#include <string.h>
#include <sys/un.h>

StaticSocketAddress &
StaticSocketAddress::operator=(const SocketAddress &src)
{
    assert(!src.IsNull());
    assert(src.GetSize() <= sizeof(address));

    size = src.GetSize();
    memcpy(&address, src.GetAddress(), size);

    return *this;
}

void
StaticSocketAddress::SetLocal(const char *path)
{
    auto &sun = reinterpret_cast<struct sockaddr_un &>(address);

    const size_t path_length = strlen(path);

    assert(path_length < sizeof(sun.sun_path));

    sun.sun_family = AF_LOCAL;
    memcpy(sun.sun_path, path, path_length + 1);

    if (sun.sun_path[0] == '@')
        /* abstract socket address */
        sun.sun_path[0] = 0;

    size = SUN_LEN(&sun);
}

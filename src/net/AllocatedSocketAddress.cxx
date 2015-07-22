/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AllocatedSocketAddress.hxx"

#include <assert.h>
#include <sys/un.h>
#include <string.h>

AllocatedSocketAddress::AllocatedSocketAddress(SocketAddress _address)
    :AllocatedSocketAddress()
{
    assert(!_address.IsNull());

    SetSize(_address.GetSize());
    memcpy(address, _address.GetAddress(), size);
}

void
AllocatedSocketAddress::SetSize(size_t new_size)
{
    if (size == new_size)
        return;

    free(address);
    size = new_size;
    address = (struct sockaddr *)malloc(size);
}

void
AllocatedSocketAddress::SetLocal(const char *path)
{
    const bool is_abstract = *path == '@';

    /* sun_path must be null-terminated unless it's an abstract
       socket */
    const size_t path_length = strlen(path) + !is_abstract;

    struct sockaddr_un *sun;
    SetSize(sizeof(*sun) - sizeof(sun->sun_path) + path_length);
    sun = (struct sockaddr_un *)address;
    sun->sun_family = AF_UNIX;
    memcpy(sun->sun_path, path, path_length);

    if (is_abstract)
        sun->sun_path[0] = 0;
}

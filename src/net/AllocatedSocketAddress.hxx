/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ALLOCATED_SOCKET_ADDRESS_HXX
#define ALLOCATED_SOCKET_ADDRESS_HXX

#include "SocketAddress.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <algorithm>

#include <stddef.h>
#include <stdlib.h>

struct sockaddr;
class Error;

class AllocatedSocketAddress {
    friend class SocketDescriptor;

    struct sockaddr *address;
    socklen_t size;

    AllocatedSocketAddress(struct sockaddr *_address,
                           socklen_t _size)
        :address(_address), size(_size) {}

public:
    AllocatedSocketAddress():address(nullptr), size(0) {}

    AllocatedSocketAddress(AllocatedSocketAddress &&src)
        :address(src.address), size(src.size) {
        src.address = nullptr;
        src.size = 0;
    }

    ~AllocatedSocketAddress() {
        free(address);
    }

    AllocatedSocketAddress &operator=(AllocatedSocketAddress &&src) {
        std::swap(address, src.address);
        std::swap(size, src.size);
        return *this;
    }

    gcc_const
    static AllocatedSocketAddress Null() {
        return AllocatedSocketAddress(nullptr, 0);
    }

    bool IsNull() const {
        return address == nullptr;
    }

    size_t GetSize() const {
        return size;
    }

    const struct sockaddr *GetAddress() const {
        return address;
    }

    operator SocketAddress() const {
        return SocketAddress(address, size);
    }

    operator const struct sockaddr *() const {
        return address;
    }

    int GetFamily() const {
        return address->sa_family;
    }

    /**
     * Does the object have a well-defined address?  Check !IsNull()
     * before calling this method.
     */
    bool IsDefined() const {
        return GetFamily() != AF_UNSPEC;
    }

    void Clear() {
        free(address);
        address = nullptr;
        size = 0;
    }

    /**
     * Make this a "local" address (UNIX domain socket).
     */
    void SetLocal(const char *path);

    bool Parse(const char *p, int default_port,
               bool passive, GError **error_r);

private:
    void SetSize(size_t new_size);
};

#endif

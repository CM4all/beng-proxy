/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef NET_LISTENER_CONFIG_HXX
#define NET_LISTENER_CONFIG_HXX

#include "net/AllocatedSocketAddress.hxx"

#include <string>

struct ListenerConfig {
    AllocatedSocketAddress bind_address;

    /**
     * If non-empty, sets SO_BINDTODEVICE.
     */
    std::string interface;

    bool reuse_port = false;

    bool free_bind = false;

    ListenerConfig() = default;

    explicit ListenerConfig(SocketAddress _bind_address)
        :bind_address(_bind_address) {}

    const char *GetInterface() const {
        return interface.empty()
            ? nullptr
            : interface.c_str();
    }
};

#endif

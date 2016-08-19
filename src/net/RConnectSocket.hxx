/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RESOLVE_CONNECT_SOCKET_HXX
#define BENG_PROXY_RESOLVE_CONNECT_SOCKET_HXX

struct addrinfo;
class SocketDescriptor;

SocketDescriptor
ResolveConnectSocket(const char *host_and_port, int default_port,
                     const struct addrinfo &hints);

#endif

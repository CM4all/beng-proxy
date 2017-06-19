/*
 * Wrapper for a socket file descriptor with input buffer management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SOCKET_PROTOCOL_ERROR_HXX
#define SOCKET_PROTOCOL_ERROR_HXX

#include <stdexcept>

class SocketProtocolError : public std::runtime_error {
public:
    SocketProtocolError(const char *msg):std::runtime_error(msg) {}
};

class SocketClosedPrematurelyError : public SocketProtocolError {
public:
    SocketClosedPrematurelyError()
        :SocketProtocolError("Peer closed the socket prematurely") {}
};

class SocketBufferFullError : public SocketProtocolError {
public:
    SocketBufferFullError()
        :SocketProtocolError("Socket buffer overflow") {}
};

class SocketTimeoutError : public SocketProtocolError {
public:
    SocketTimeoutError()
        :SocketProtocolError("Timeout") {}
};

#endif

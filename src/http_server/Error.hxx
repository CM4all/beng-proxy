/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_HTTP_SERVER_ERROR_HXX
#define BENG_HTTP_SERVER_ERROR_HXX

#include <exception>
#include <stdexcept>

class HttpServerSocketError : public std::nested_exception {};

class HttpServerProtocolError : public std::runtime_error {
public:
    explicit HttpServerProtocolError(const char *msg):std::runtime_error(msg) {}
};

#endif

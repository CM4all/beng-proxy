/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_MESSAGE_RESPONSE_HXX
#define BENG_PROXY_HTTP_MESSAGE_RESPONSE_HXX

#include <http/status.h>

#include <stdexcept>

/**
 * An exception which can be thrown to indicate that a certain HTTP
 * response shall be sent to our HTTP client.
 */
class HttpMessageResponse : public std::runtime_error {
    http_status_t status;

public:
    HttpMessageResponse(http_status_t _status, const char *_msg)
        :std::runtime_error(_msg), status(_status) {}

    http_status_t GetStatus() const {
        return status;
    }
};

#endif

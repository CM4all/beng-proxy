/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_SIMPLE_HTTP_RESPONSE_HXX
#define BENG_LB_SIMPLE_HTTP_RESPONSE_HXX

#include <http/status.h>

#include <string>

struct LbSimpleHttpResponse {
    http_status_t status = http_status_t(0);

    /**
     * The "Location" response header.
     */
    std::string location;

    std::string message;

    LbSimpleHttpResponse() = default;
    explicit LbSimpleHttpResponse(http_status_t _status)
        :status(_status) {}

    bool IsDefined() const {
        return status != http_status_t(0);
    }
};

#endif

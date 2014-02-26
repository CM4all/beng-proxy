/*
 * The translation request struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_REQUEST_HXX
#define BENG_PROXY_TRANSLATE_REQUEST_HXX

#include "util/ConstBuffer.hxx"
#include "strref.h"

#include <http/status.h>

#include <stddef.h>
#include <stdint.h>

struct TranslateRequest {
    const struct sockaddr *local_address;
    size_t local_address_length;

    const char *remote_host;
    const char *host;
    const char *user_agent;
    const char *ua_class;
    const char *accept_language;

    /**
     * The value of the "Authorization" HTTP request header.
     */
    const char *authorization;

    const char *uri;
    const char *args;
    const char *query_string;
    const char *widget_type;
    const char *session;
    const char *param;

    /**
     * The payload of the CHECK packet.  If
     * strref_is_null(&response.check), then no CHECK packet will be
     * sent.
     */
    struct strref check;

    /**
     * The payload of the #TRANSLATE_WANT_FULL_URI packet.  If
     * strref_is_null(&response.want_full_uri), then no
     * #TRANSLATE_WANT_FULL_URI packet was received.
     */
    struct strref want_full_uri;

    ConstBuffer<uint16_t> want;

    http_status_t error_document_status;
};

#endif

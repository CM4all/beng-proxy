/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REQUEST_H
#define __BENG_REQUEST_H

#include "uri.h"
#include "translate.h"

struct request {
    struct http_server_request *request;
    struct parsed_uri uri;
    struct {
        struct translate_request request;
        const struct translate_response *response;
    } translate;
};

#endif

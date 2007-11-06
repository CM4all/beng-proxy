/*
 * Definition of the HTTP response handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_RESPONSE_H
#define __BENG_HTTP_RESPONSE_H

#include "strmap.h"
#include "istream.h"
#include "http.h"

#include <stddef.h>

struct http_client_response_handler {
    void (*response)(http_status_t status, strmap_t headers,
                     off_t content_length, istream_t body,
                     void *ctx);
    void (*free)(void *ctx);
};

#endif

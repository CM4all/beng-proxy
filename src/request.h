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
#include "strmap.h"
#include "processor.h"
#include "url-stream.h"

struct request {
    struct http_server_request *request;
    struct parsed_uri uri;

    strmap_t args;

    char session_id_buffer[9];
    struct session *session;

    struct {
        struct translate_request request;
        const struct translate_response *response;
    } translate;

    struct processor_env env;

    url_stream_t url_stream;

    unsigned response_sent;
};

void
request_get_session(struct request *request, const char *session_id);

struct session *
request_make_session(struct request *request);


extern const struct http_response_handler response_handler;

#endif

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
#include "async.h"

struct request {
    struct tcache *translate_cache;

    struct hstock *http_client_stock;
    struct http_cache *http_cache;

    struct http_server_request *request;
    struct parsed_uri uri;

    strmap_t args;

    struct strmap *cookies;
    char session_id_buffer[9];
    struct session *session;

    struct {
        struct translate_request request;
        const struct translate_response *response;
        const struct translate_transformation *transformation;
    } translate;

    struct processor_env env;

    unsigned body_consumed, response_sent;
    struct async_operation_ref *async_ref;
};

int
request_processor_enabled(struct request *request);

int
response_dispatcher_wants_body(struct request *request);

void
request_get_session(struct request *request, const char *session_id);

void
request_get_cookie_session(struct request *request);

struct session *
request_make_session(struct request *request);


struct growing_buffer;

void
response_dispatch(struct request *request2,
                  http_status_t status, struct growing_buffer *headers,
                  istream_t body);

extern const struct http_response_handler response_handler;

#endif

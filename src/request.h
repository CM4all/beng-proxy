/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REQUEST_H
#define __BENG_REQUEST_H

#include "uri-parser.h"
#include "translate.h"
#include "processor.h"
#include "async.h"
#include "session.h"
#include "transformation.h"

struct request {
    struct http_server_request *request;
    struct parsed_uri uri;

    struct strmap *args;

    struct strmap *cookies;
    char session_id_buffer[9];
    session_id_t session_id;

    struct {
        struct translate_request request;
        const struct translate_response *response;
        const struct transformation *transformation;
    } translate;

    /**
     * An identifier for the source stream of the current
     * transformation.  This is used by the filter cache to address
     * resources.
     */
    const char *resource_tag;

    struct processor_env env;

#ifdef DUMP_WIDGET_TREE
    const struct widget *dump_widget_tree;
#endif

    bool body_consumed, response_sent;
    struct async_operation_ref *async_ref;
};

static inline bool
request_transformation_enabled(struct request *request)
{
    return request->translate.response->views->transformation != NULL;
}

bool
request_processor_enabled(struct request *request);

bool
response_dispatcher_wants_body(struct request *request);

/**
 * Discard the request body if it was not used yet.  Call this before
 * sending the response to the HTTP server library.
 */
void
request_discard_body(struct request *request);

void
request_args_parse(struct request *request);

void
request_determine_session(struct request *request);

struct session *
request_make_session(struct request *request);


struct growing_buffer;

void
response_dispatch(struct request *request2,
                  http_status_t status, struct growing_buffer *headers,
                  istream_t body);

extern const struct http_response_handler response_handler;

#endif

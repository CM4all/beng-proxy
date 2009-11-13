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
    session_id_t session_id;
    struct session_id_string session_id_string;
    bool send_session_cookie;

    bool stateless;

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

    bool body_consumed;

    /**
     * Is the processor active, and is there a focused widget?
     */
    bool processor_focus;

#ifndef NDEBUG
    bool response_sent;
#endif

    struct async_operation_ref *async_ref;
};

static inline bool
request_transformation_enabled(const struct request *request)
{
    return request->translate.response->views->transformation != NULL;
}

/**
 * Returns true if the first transformation (if any) is the processor.
 */
static inline bool
request_processor_first(const struct request *request)
{
    return request_transformation_enabled(request) &&
        request->translate.response->views->transformation->type
        == TRANSFORMATION_PROCESS;
}

bool
request_processor_enabled(const struct request *request);

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

static inline struct session *
request_get_session(const struct request *request)
{
    return request->session_id != 0
        ? session_get(request->session_id)
        : NULL;
}

struct session *
request_make_session(struct request *request);

void
request_discard_session(struct request *request);


struct growing_buffer;

void
response_dispatch(struct request *request2,
                  http_status_t status, struct growing_buffer *headers,
                  istream_t body);

extern const struct http_response_handler response_handler;

#endif

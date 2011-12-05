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
#include "widget-class.h"

struct request {
    struct client_connection *connection;

    struct http_server_request *request;
    struct parsed_uri uri;

    struct strmap *args;

    struct strmap *cookies;
    session_id_t session_id;
    struct session_id_string session_id_string;
    bool send_session_cookie;

    /**
     * The realm name of the request.  This is valid only after the
     * translation server has responded, because the translation
     * server may override it.
     */
    const char *realm;

    /**
     * The realm name of the session.
     */
    const char *session_realm;

    bool stateless;

    struct {
        struct translate_request request;
        const struct translate_response *response;
        const struct transformation *transformation;

        /**
         * A pointer to the "previous" translate response, non-NULL
         * only if beng-proxy sends a second translate request with a
         * CHECK packet.
         */
        const struct translate_response *previous;

        /**
         * Number of CHECK packets followed so far.  This variable is
         * used for loop detection.
         */
        unsigned checks;
    } translate;

    /**
     * The product token (RFC 2616 3.8) being forwarded; NULL if
     * beng-proxy shall generate one.
     */
    const char *product_token;

    /**
     * An identifier for the source stream of the current
     * transformation.  This is used by the filter cache to address
     * resources.
     */
    const char *resource_tag;

    struct processor_env env;

    /**
     * The widget currently being processed by
     * processor_lookup_widget() or widget_resolver_new(), see
     * proxy-widget.c.
     */
    struct widget *widget;

    /**
     * A reference to the widget that should be proxied.  Used by
     * proxy_widget().
     */
    const struct widget_ref *proxy_ref;

    /**
     * A pointer to the request body, or NULL if there is none.  Once
     * the request body has been "used", this pointer gets cleared.
     */
    struct istream *body;

    /**
     * Is the processor active, and is there a focused widget?
     */
    bool processor_focus;

    /**
     * Was the response already transformed?  The error document only
     * applies to the original, untransformed response.
     */
    bool transformed;

#ifndef NDEBUG
    bool response_sent;
#endif

    /**
     * This attribute represents the operation that handles the HTTP
     * request.  It is used to clean up resources on abort.
     */
    struct async_operation operation;

    struct async_operation_ref async_ref;
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
    return session_id_is_defined(request->session_id)
        ? session_get(request->session_id)
        : NULL;
}

struct session *
request_make_session(struct request *request);

void
request_ignore_session(struct request *request);

void
request_discard_session(struct request *request);


struct growing_buffer;

void
response_dispatch(struct request *request2,
                  http_status_t status, struct growing_buffer *headers,
                  istream_t body);

void
response_dispatch_message(struct request *request2, http_status_t status,
                          const char *msg);

void
response_dispatch_message2(struct request *request2, http_status_t status,
                           struct growing_buffer *headers, const char *msg);

void
response_dispatch_error(struct request *request, GError *error);

void
response_dispatch_redirect(struct request *request2, http_status_t status,
                           const char *location, const char *msg);

extern const struct http_response_handler response_handler;

#endif

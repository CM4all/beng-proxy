/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_REQUEST_HXX
#define BENG_PROXY_REQUEST_HXX

#include "uri-parser.h"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "penv.h"
#include "async.h"
#include "session.h"
#include "transformation.h"
#include "widget-class.h"

#include <glib.h>

struct request {
    struct client_connection *connection;

    struct http_server_request *request;
    struct parsed_uri uri;

    struct strmap *args;

    struct strmap *cookies;

    /**
     * The name of the session cookie.
     */
    const char *session_cookie;

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

    /**
     * Is this request "stateless", i.e. is session management
     * disabled?  This is initialized by request_determine_session(),
     * and may be disabled later by handle_translated_request().
     */
    bool stateless;

    struct {
        struct translate_request request;
        const struct translate_response *response;
        const struct transformation *transformation;

        /**
         * A pointer to the "previous" translate response, non-nullptr
         * only if beng-proxy sends a second translate request with a
         * CHECK packet.
         */
        const struct translate_response *previous;

        /**
         * Number of CHECK packets followed so far.  This variable is
         * used for loop detection.
         */
        unsigned checks;

        /**
         * True when the translation server has sent
         * #TRANSLATE_WANT_FULL_URI.
         */
        bool want_full_uri;
    } translate;

    /**
     * The product token (RFC 2616 3.8) being forwarded; nullptr if
     * beng-proxy shall generate one.
     */
    const char *product_token;

#ifndef NO_DATE_HEADER
    /**
     * The "date" response header (RFC 2616 14.18) being forwarded;
     * nullptr if beng-proxy shall generate one.
     */
    const char *date;
#endif

    /**
     * An identifier for the source stream of the current
     * transformation.  This is used by the filter cache to address
     * resources.
     */
    const char *resource_tag;

    struct processor_env env;

    /**
     * A pointer to the request body, or nullptr if there is none.  Once
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
    return request->translate.response->views->transformation != nullptr;
}

/**
 * Returns true if the first transformation (if any) is the processor.
 */
static inline bool
request_processor_first(const struct request *request)
{
    return request_transformation_enabled(request) &&
        request->translate.response->views->transformation->type
        == transformation::TRANSFORMATION_PROCESS;
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
        : nullptr;
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
                  struct istream *body);

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

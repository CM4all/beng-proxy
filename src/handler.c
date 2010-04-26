/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "file-handler.h"
#include "request.h"
#include "connection.h"
#include "config.h"
#include "args.h"
#include "session.h"
#include "instance.h"
#include "tcache.h"
#include "growing-buffer.h"
#include "header-writer.h"
#include "strref-pool.h"
#include "dpool.h"
#include "http-server.h"
#include "transformation.h"
#include "expiry.h"
#include "uri-escape.h"

#include <daemon/log.h>

#include <assert.h>

static const char *
bounce_uri(pool_t pool, const struct request *request,
           const struct translate_response *response)
{
    const char *scheme = response->scheme != NULL
        ? response->scheme : "http";
    const char *host = response->host != NULL
        ? response->host
        : strmap_get(request->request->headers, "host");
    if (host == NULL)
        host = "localhost";

    const char *uri_path = response->uri != NULL
        ? p_strncat(pool, response->uri, strlen(response->uri),
                    ";", strref_is_empty(&request->uri.args) ? (size_t)0 : 1,
                    request->uri.args.data, request->uri.args.length,
                    "?", strref_is_empty(&request->uri.query) ? (size_t)0 : 1,
                    request->uri.query.data, request->uri.query.length,
                    NULL)
        : request->request->uri;

    const char *current_uri = p_strcat(pool, scheme, "://", host, uri_path,
                                       NULL);
    const char *escaped_uri = uri_escape_dup(pool, current_uri,
                                             strlen(current_uri));

    return p_strcat(pool, response->bounce, escaped_uri, NULL);
}

static void
translate_callback(const struct translate_response *response,
                   void *ctx)
{
    struct request *request = ctx;
    struct session *session;

    request->connection->site_name = response->site;

    if (response->discard_session)
        request_discard_session(request);

    request->translate.response = response;
    request->translate.transformation = response->views != NULL
        ? response->views->transformation
        : NULL;

    if (response->request_header_forward.modes[HEADER_GROUP_COOKIE] != HEADER_FORWARD_MANGLE ||
        response->response_header_forward.modes[HEADER_GROUP_COOKIE] != HEADER_FORWARD_MANGLE) {
        /* disable session management if cookies are not mangled by
           beng-proxy */
        session_id_clear(&request->session_id);
        request->stateless = true;
    }

    if (response->www_authenticate != NULL) {
        pool_t pool = request->request->pool;
        struct growing_buffer *headers = growing_buffer_new(pool, 256);
        header_write(headers, "www-authenticate", response->www_authenticate);

        http_server_response(request->request,
                             HTTP_STATUS_UNAUTHORIZED, headers,
                             istream_string_new(pool, "Unauthorized"));
        return;
    }

    if (response->status == (http_status_t)-1 ||
        (response->status == (http_status_t)0 &&
         response->address.type == RESOURCE_ADDRESS_NONE &&
         response->bounce == NULL &&
         response->redirect == NULL)) {
        request_discard_body(request);
        http_server_send_message(request->request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return;
    }

    if (response->session != NULL || response->user != NULL ||
        response->language != NULL || response->views->transformation != NULL)
        session = request_get_session(request);
    else
        session = NULL;

    if (response->session != NULL) {
        if (*response->session == 0) {
            /* clear translate session */

            if (session != NULL)
                session_clear_translate(session);
        } else {
            /* set new translate session */

            if (session == NULL)
                session = request_make_session(request);

            if (session != NULL)
                session_set_translate(session, response->session);
        }
    }

    if (response->user != NULL) {
        if (*response->user == 0) {
            /* log out */

            if (session != NULL)
                session_clear_user(session);
        } else {
            /* log in */

            if (session == NULL)
                session = request_make_session(request);

            if (session != NULL)
                session_set_user(session, response->user,
                                 response->user_max_age);
        }
    } else if (session != NULL && session->user != NULL && session->user_expires > 0 &&
               is_expired(session->user_expires)) {
        daemon_log(4, "user '%s' has expired\n", session->user);
        d_free(session->pool, session->user);
        session->user = NULL;
    }

    if (response->language != NULL) {
        if (*response->language == 0) {
            /* reset language setting */

            if (session != NULL)
                session_clear_language(session);
        } else {
            /* override language */

            if (session == NULL)
                session = request_make_session(request);

            if (session != NULL)
                session_set_language(session, response->language);
        }
    }

    /* always enforce sessions when the processor is enabled */
    if (request_processor_enabled(request) && session == NULL)
        session = request_make_session(request);

    if (session != NULL)
        session_put(session);

    request->resource_tag = resource_address_id(&response->address,
                                                request->request->pool);

    request->processor_focus = request->args != NULL &&
        request_processor_enabled(request) &&
        strmap_get(request->args, "focus") != NULL;

    if (response->address.type == RESOURCE_ADDRESS_LOCAL) {
        if (response->address.u.local.delegate != NULL)
            delegate_handler(request);
        else
            file_callback(request);
    } else if (response->address.type == RESOURCE_ADDRESS_CGI) {
        cgi_handler(request);
    } else if (response->address.type == RESOURCE_ADDRESS_HTTP) {
        proxy_handler(request);
    } else if (response->address.type == RESOURCE_ADDRESS_AJP) {
        ajp_handler(request);
    } else if (response->address.type == RESOURCE_ADDRESS_FASTCGI) {
        fcgi_handler(request);
    } else if (response->redirect != NULL) {
        request_discard_body(request);

        int status = response->status != 0
            ? response->status : HTTP_STATUS_SEE_OTHER;
        http_server_send_redirect(request->request, status,
                                  response->redirect, NULL);
    } else if (response->bounce != NULL) {
        request_discard_body(request);
        http_server_send_redirect(request->request, HTTP_STATUS_SEE_OTHER,
                                  bounce_uri(request->request->pool, request,
                                             response),
                                  NULL);
    } else if (response->status != (http_status_t)0) {
        request_discard_body(request);
        http_server_response(request->request,
                             response->status,
                             NULL, NULL);
    } else {
        daemon_log(2, "empty response from translation server\n");

        request_discard_body(request);
        http_server_send_message(request->request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
    }
}

static bool
request_uri_parse(struct http_server_request *request,
                  struct parsed_uri *dest)
{
    bool ret;

    ret = uri_parse(dest, request->uri);
    if (!ret) {
        if (request->body != NULL)
            istream_close(request->body);

        http_server_send_message(request,
                                 HTTP_STATUS_BAD_REQUEST,
                                 "Malformed URI");
    }

    return ret;
}

static void
fill_translate_request(struct translate_request *t,
                       const struct http_server_request *request,
                       const struct parsed_uri *uri,
                       struct strmap *args)
{
    t->local_address = request->local_address;
    t->local_address_length = request->local_address_length;
    t->remote_host = request->remote_host;
    t->host = strmap_get(request->headers, "host");
    t->user_agent = strmap_get(request->headers, "user-agent");
    t->accept_language = strmap_get(request->headers, "accept-language");
    t->authorization = strmap_get(request->headers, "authorization");
    t->uri = strref_dup(request->pool, &uri->base);
    t->args = args != NULL
        ? args_format(request->pool, args,
                      NULL, NULL, NULL, NULL,
                      "translate")
        : NULL;
    if (t->args != NULL && *t->args == 0)
        t->args = NULL;

    t->query_string = strref_is_empty(&uri->query)
        ? NULL
        : strref_dup(request->pool, &uri->query);
    t->widget_type = NULL;
}

static void
ask_translation_server(struct request *request2, struct tcache *tcache)
{
    struct http_server_request *request = request2->request;

    fill_translate_request(&request2->translate.request, request2->request,
                           &request2->uri, request2->args);
    translate_cache(request->pool, tcache, &request2->translate.request,
                    translate_callback, request2,
                    request2->async_ref);
}

static void
serve_document_root_file(struct request *request2,
                         const struct config *config)
{
    struct http_server_request *request = request2->request;
    struct parsed_uri *uri;
    struct translate_response *tr;
    const char *index_file = NULL;
    bool process;

    uri = &request2->uri;

    request2->translate.response = tr = p_malloc(request->pool,
                                                 sizeof(*request2->translate.response));

    if (uri->base.data[uri->base.length - 1] == '/') {
        index_file = "index.html";
        process = true;
    } else {
        process = strref_ends_with_n(&uri->base, ".html", 5);
    }

    if (process) {
        struct transformation *transformation = p_malloc(request->pool, sizeof(*transformation));
        struct transformation_view *view = p_malloc(request->pool, sizeof(*view));

        transformation->next = NULL;
        transformation->type = TRANSFORMATION_PROCESS;

        view->next = NULL;
        view->name = NULL;
        view->transformation = transformation;

        tr->views = view;
    } else {
        struct transformation_view *view = p_calloc(request->pool, sizeof(*view));

        tr->views = view;
    }

    request2->translate.transformation = tr->views->transformation;

    tr->status = 0;
    tr->address.type = RESOURCE_ADDRESS_LOCAL;
    tr->address.u.local.path = p_strncat(request->pool,
                                         config->document_root,
                                         strlen(config->document_root),
                                         uri->base.data,
                                         uri->base.length,
                                         index_file, (size_t)10,
                                         NULL);
    tr->address.u.local.delegate = NULL;
    tr->address.u.local.content_type = NULL;
    tr->address.u.local.document_root = NULL;

    tr->request_header_forward = (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
        },
    };

    tr->response_header_forward = (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
        },
    };

    tr->scheme = NULL;
    tr->host = NULL;
    tr->uri = NULL;

    request2->resource_tag = tr->address.u.local.path;
    request2->processor_focus = process &&
        strmap_get_checked(request2->args, "focus") != NULL;

    file_callback(request2);
}

void
handle_http_request(struct client_connection *connection,
                    struct http_server_request *request,
                    struct async_operation_ref *async_ref)
{
    struct request *request2;
    bool ret;

    assert(request != NULL);

    request2 = p_malloc(request->pool, sizeof(*request2));
    request2->connection = connection;
    request2->request = request;

    ret = request_uri_parse(request, &request2->uri);
    if (!ret)
        return;

    assert(!strref_is_empty(&request2->uri.base));
    assert(request2->uri.base.data[0] == '/');

    request2->args = NULL;
    request2->cookies = NULL;
    session_id_clear(&request2->session_id);
    request2->send_session_cookie = NULL;
#ifdef DUMP_WIDGET_TREE
    request2->dump_widget_tree = NULL;
#endif
    request2->body_consumed = false;

#ifndef NDEBUG
    request2->response_sent = false;
#endif

    request2->async_ref = async_ref;

    request_args_parse(request2);
    request_determine_session(request2);

    if (connection->instance->translate_cache == NULL)
        serve_document_root_file(request2, connection->config);
    else
        ask_translation_server(request2, connection->instance->translate_cache);
}

/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
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

#include <daemon/log.h>

#include <assert.h>

/**
 * The request had no valid session, but it requires one; redirect the
 * client to a new URI which includes the session id, and send a
 * session cookie.
 */
static void
session_redirect(struct request *request)
{
    struct session *session;
    struct growing_buffer *headers =
        growing_buffer_new(request->request->pool, 512);
    char session_id[9];
    const char *args;

    session = request_make_session(request);
    assert(session != NULL);

    session_id_format(session_id, session->uri_id);
    args = args_format(request->request->pool, request->args,
                       "session", session_id, NULL, NULL, NULL);
    header_write(headers, "location",
                 p_strncat(request->request->pool,
                           request->uri.base.data, request->uri.base.length,
                           ";", (size_t)1,
                           args, strlen(args),
                           request->uri.query.length == 0 ? NULL : "?",
                           (size_t)1,
                           request->uri.query.data, request->uri.query.length,
                           NULL));

    session_id_format(session_id, session->cookie_id);
    header_write(headers, "set-cookie",
                 p_strcat(request->request->pool,
                          "beng_proxy_session=", session_id,
                          "; Discard; HttpOnly; Path=/; Version=1", NULL));

    session->cookie_sent = true;

    http_server_response(request->request,
                         request->request->method == HTTP_METHOD_GET
                         ? HTTP_STATUS_FOUND
                         : HTTP_STATUS_TEMPORARY_REDIRECT,
                         headers, NULL);
}

static void
translate_callback(const struct translate_response *response,
                   void *ctx)
{
    struct request *request = ctx;
    struct session *session;

    request->translate.response = response;
    request->translate.transformation = response->transformation;

    if (response->status == (http_status_t)-1 ||
        (response->status == (http_status_t)0 &&
         response->address.type == RESOURCE_ADDRESS_NONE &&
         response->redirect == NULL)) {
        http_server_send_message(request->request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        return;
    }

    if (request->session_id != 0 &&
        (response->session != NULL || response->user != NULL ||
         response->language != NULL || response->transformation != NULL))
        session = session_get(request->session_id);
    else
        session = NULL;

    if (response->session != NULL) {
        if (*response->session == 0) {
            /* clear translate session */

            if (session != NULL)
                session->translate = NULL;
        } else {
            /* set new translate session */

            request_make_session(request);

            if (session->translate == NULL ||
                strcmp(response->session, session->translate) != 0)
                session->translate = d_strdup(session->pool, response->session);
        }
    }

    if (response->user != NULL) {
        if (*response->user == 0) {
            /* log out */

            if (session != NULL)
                session->user = NULL;
        } else {
            /* log in */

            request_make_session(request);

            if (session->user == NULL ||
                strcmp(response->user, session->user) != 0)
                session->user = d_strdup(session->pool, response->user);
        }
    }

    if (response->language != NULL) {
        if (*response->language == 0) {
            /* reset language setting */

            if (session != NULL)
                session->language = NULL;
        } else {
            /* override language */

            request_make_session(request);

            if (session->language == NULL ||
                strcmp(response->language, session->language) != 0)
                session->language = d_strdup(session->pool, response->language);
        }
    }

    /* always enforce sessions when there is a transformation
       (e.g. the beng template processor); also redirect the client
       when a session has just been created */
    if ((response->transformation != NULL && session == NULL) ||
        (session != NULL && !session->cookie_sent)) {
        session_redirect(request);
        return;
    }

    if (response->address.type == RESOURCE_ADDRESS_LOCAL) {
        file_callback(request);
    } else if (response->address.type == RESOURCE_ADDRESS_CGI) {
        cgi_handler(request);
    } else if (response->address.type == RESOURCE_ADDRESS_HTTP) {
        proxy_handler(request);
    } else if (response->redirect != NULL) {
        http_server_send_redirect(request->request, HTTP_STATUS_SEE_OTHER,
                                  response->redirect, NULL);
    } else if (response->status != (http_status_t)0) {
        http_server_response(request->request,
                             response->status,
                             NULL, NULL);
    } else {
        daemon_log(2, "empty response from translation server\n");
        http_server_send_message(request->request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
    }
}

static int
request_uri_parse(struct http_server_request *request,
                  struct parsed_uri *dest)
{
    int ret;

    ret = uri_parse(request->pool, dest, request->uri);
    if (ret < 0)
        http_server_send_message(request,
                                 HTTP_STATUS_BAD_REQUEST,
                                 "Malformed URI");

    return ret;
}

static void
request_args_parse(struct request *request)
{
    const char *session_id;

    assert(request != NULL);
    assert(request->args == NULL);

    if (strref_is_empty(&request->uri.args)) {
        request->args = NULL;
        request->translate.request.param = NULL;
        request->translate.request.session = NULL;
        return;
    }

    request->args = args_parse(request->request->pool,
                               request->uri.args.data, request->uri.args.length);
    request->translate.request.param = strmap_remove(request->args, "translate");
    request->translate.request.session = NULL;

    session_id = strmap_get(request->args, "session");
    if (session_id != NULL)
        request_get_session(request, session_id);
}

static void
ask_translation_server(struct request *request2, struct tcache *tcache)
{
    struct http_server_request *request = request2->request;

    request2->translate.request.remote_host = request->remote_host;
    request2->translate.request.host = strmap_get(request->headers, "host");
    request2->translate.request.uri = strref_dup(request->pool,
                                                 &request2->uri.base);
    request2->translate.request.widget_type = NULL;

    translate_cache(request->pool, tcache, &request2->translate.request,
                    translate_callback, request2,
                    request2->async_ref);
}

static bool
request_session_cookie_sent(struct request *request)
{
    struct session *session;

    if (request->session_id == 0)
        return false;

    session = session_get(request->session_id);
    return session != NULL && session->cookie_sent;
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

    if (process && !request_session_cookie_sent(request2)) {
        session_redirect(request2);
        return;
    }

    if (process) {
        struct translate_transformation *transformation = p_malloc(request->pool, sizeof(*transformation));
        transformation->next = NULL;
        transformation->type = TRANSFORMATION_PROCESS;
        tr->transformation = transformation;
    } else
        tr->transformation = NULL;

    request2->translate.transformation = tr->transformation;

    tr->status = 0;
    tr->address.type = RESOURCE_ADDRESS_LOCAL;
    tr->address.u.local.path = p_strncat(request->pool,
                                         config->document_root,
                                         strlen(config->document_root),
                                         uri->base.data,
                                         uri->base.length,
                                         index_file, (size_t)10,
                                         NULL);
    tr->address.u.local.content_type = NULL;

    file_callback(request2);
}

void
handle_http_request(struct client_connection *connection,
                    struct http_server_request *request,
                    struct async_operation_ref *async_ref)
{
    struct request *request2;
    int ret;

    assert(request != NULL);

    request2 = p_malloc(request->pool, sizeof(*request2));
    request2->request = request;

    ret = request_uri_parse(request, &request2->uri);
    if (ret < 0)
        return;

    assert(!strref_is_empty(&request2->uri.base));
    assert(request2->uri.base.data[0] == '/');

    request2->args = NULL;
    request2->cookies = NULL;
    request2->session_id = 0;
#ifdef DUMP_WIDGET_TREE
    request2->dump_widget_tree = NULL;
#endif
    request2->body_consumed = false;
    request2->response_sent = false;
    request2->async_ref = async_ref;

    request_args_parse(request2);
    if (request2->session_id != 0) {
        struct session *session = session_get(request2->session_id);

        if (session != NULL) {
            session_id_t id = request_get_cookie_session_id(request2);
            if (id == session->cookie_id)
                session->cookie_received = true;
            else if (session->cookie_received)
                /* someone has stolen our URI including the session
                   id; refuse to continue with this session */
                request2->session_id = 0;
        }
    }

    if (connection->instance->translate_cache == NULL)
        serve_document_root_file(request2, connection->config);
    else
        ask_translation_server(request2, connection->instance->translate_cache);
}

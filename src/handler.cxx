/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.hxx"
#include "config.hxx"
#include "bp_instance.hxx"
#include "cast.hxx"
#include "file_handler.hxx"
#include "nfs_handler.h"
#include "request.hxx"
#include "connection.h"
#include "args.h"
#include "session.h"
#include "tcache.hxx"
#include "growing-buffer.h"
#include "header-writer.h"
#include "strref-pool.h"
#include "dpool.h"
#include "http_server.h"
#include "transformation.h"
#include "expiry.h"
#include "uri-escape.h"
#include "uri-verify.h"
#include "strutil.h"
#include "strmap.h"
#include "istream.h"
#include "translate_client.hxx"
#include "ua_classification.h"

#include <daemon/log.h>

#include <assert.h>

static unsigned translation_protocol_version;

static const char *
bounce_uri(struct pool *pool, const struct request *request,
           const TranslateResponse &response)
{
    const char *scheme = response.scheme != nullptr
        ? response.scheme : "http";
    const char *host = response.host != nullptr
        ? response.host
        : strmap_get(request->request->headers, "host");
    if (host == nullptr)
        host = "localhost";

    const char *uri_path = response.uri != nullptr
        ? p_strncat(pool, response.uri, strlen(response.uri),
                    ";", strref_is_empty(&request->uri.args) ? (size_t)0 : 1,
                    request->uri.args.data, request->uri.args.length,
                    "?", strref_is_empty(&request->uri.query) ? (size_t)0 : 1,
                    request->uri.query.data, request->uri.query.length,
                    nullptr)
        : request->request->uri;

    const char *current_uri = p_strcat(pool, scheme, "://", host, uri_path,
                                       nullptr);
    const char *escaped_uri = uri_escape_dup(pool, current_uri,
                                             strlen(current_uri), '%');

    return p_strcat(pool, response.bounce, escaped_uri, nullptr);
}

/**
 * Determine the realm name, consider the override by the translation
 * server.  Guaranteed to return non-nullptr.
 */
static const char *
get_request_realm(struct pool *pool, const struct strmap *request_headers,
                  const TranslateResponse &response)
{
    if (response.realm != nullptr)
        return response.realm;

    const char *host = strmap_get_checked(request_headers, "host");
    if (host != nullptr) {
        char *p = p_strdup(pool, host);
        str_to_lower(p);
        return p;
    }

    /* fall back to empty string as the default realm if there is no
       "Host" header */
    return "";
}

/**
 * Apply session-specific data from the #TranslateResponse.  Returns
 * the session object or nullptr.  The session must be freed by the
 * caller using session_put().
 */
static struct session *
apply_translate_response_session(request &request,
                                 const TranslateResponse &response)
{
    request.realm = get_request_realm(request.request->pool,
                                      request.request->headers, response);

    if (request.session_realm != nullptr &&
        strcmp(request.realm, request.session_realm) != 0) {
        daemon_log(2, "ignoring spoofed session id from another realm (request='%s', session='%s')\n",
                   request.realm, request.session_realm);
        request_ignore_session(&request);
    }

    request.connection->site_name = response.site;

    if (response.transparent) {
        session_id_clear(&request.session_id);
        request.stateless = true;
        request.args = nullptr;
    }

    if (response.discard_session)
        request_discard_session(&request);
    else if (response.transparent)
        request_ignore_session(&request);

    struct session *session;
    if (response.session != nullptr || response.user != nullptr ||
        response.language != nullptr ||
        response.views->transformation != nullptr)
        session = request_get_session(&request);
    else
        session = nullptr;

    if (response.session != nullptr) {
        if (*response.session == 0) {
            /* clear translate session */

            if (session != nullptr)
                session_clear_translate(session);
        } else {
            /* set new translate session */

            if (session == nullptr)
                session = request_make_session(&request);

            if (session != nullptr)
                session_set_translate(session, response.session);
        }
    }

    if (response.user != nullptr) {
        if (*response.user == 0) {
            /* log out */

            if (session != nullptr)
                session_clear_user(session);
        } else {
            /* log in */

            if (session == nullptr)
                session = request_make_session(&request);

            if (session != nullptr)
                session_set_user(session, response.user,
                                 response.user_max_age);
        }
    } else if (session != nullptr && session->user != nullptr && session->user_expires > 0 &&
               is_expired(session->user_expires)) {
        daemon_log(4, "user '%s' has expired\n", session->user);
        d_free(session->pool, session->user);
        session->user = nullptr;
    }

    if (response.language != nullptr) {
        if (*response.language == 0) {
            /* reset language setting */

            if (session != nullptr)
                session_clear_language(session);
        } else {
            /* override language */

            if (session == nullptr)
                session = request_make_session(&request);

            if (session != nullptr)
                session_set_language(session, response.language);
        }
    }

    return session;
}

/**
 * Called by handle_translated_request() with the #TranslateResponse
 * copy.
 */
static void
handle_translated_request2(request &request,
                           const TranslateResponse &response)
{
    request.translate.transformation = response.views != nullptr
        ? response.views->transformation
        : nullptr;

    if (response.request_header_forward.modes[HEADER_GROUP_COOKIE] != HEADER_FORWARD_MANGLE ||
        response.response_header_forward.modes[HEADER_GROUP_COOKIE] != HEADER_FORWARD_MANGLE) {
        /* disable session management if cookies are not mangled by
           beng-proxy */
        session_id_clear(&request.session_id);
        request.stateless = true;
    }

    if (response.status == (http_status_t)-1 ||
        (response.status == (http_status_t)0 &&
         response.address.type == RESOURCE_ADDRESS_NONE &&
         response.www_authenticate == nullptr &&
         response.bounce == nullptr &&
         response.redirect == nullptr)) {
        response_dispatch_message(&request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
        return;
    }

    struct session *session =
        apply_translate_response_session(request, response);

    /* always enforce sessions when the processor is enabled */
    if (request_processor_enabled(&request) && session == nullptr)
        session = request_make_session(&request);

    if (session != nullptr)
        session_put(session);

    request.resource_tag = resource_address_id(&response.address,
                                               request.request->pool);

    request.processor_focus = request.args != nullptr &&
        request_processor_enabled(&request) &&
        strmap_get(request.args, "focus") != nullptr;

    if (response.address.type == RESOURCE_ADDRESS_LOCAL) {
        if (response.address.u.file->delegate != nullptr)
            delegate_handler(request);
        else
            file_callback(&request);
#ifdef HAVE_LIBNFS
    } else if (response.address.type == RESOURCE_ADDRESS_NFS) {
        nfs_handler(&request);
#endif
    } else if (response.address.type == RESOURCE_ADDRESS_HTTP ||
               response.address.type == RESOURCE_ADDRESS_LHTTP ||
               resource_address_is_cgi_alike(&response.address) ||
               response.address.type == RESOURCE_ADDRESS_NFS ||
               response.address.type == RESOURCE_ADDRESS_AJP) {
        proxy_handler(request);
    } else if (response.redirect != nullptr) {
        http_status_t status = response.status != (http_status_t)0
            ? response.status : HTTP_STATUS_SEE_OTHER;
        response_dispatch_redirect(&request, status, response.redirect,
                                   nullptr);
    } else if (response.bounce != nullptr) {
        response_dispatch_redirect(&request, HTTP_STATUS_SEE_OTHER,
                                   bounce_uri(request.request->pool, &request,
                                              response),
                                   nullptr);
    } else if (response.status != (http_status_t)0) {
        response_dispatch(&request, response.status, nullptr, nullptr);
    } else if (response.www_authenticate != nullptr) {
        response_dispatch_message(&request, HTTP_STATUS_UNAUTHORIZED,
                                  "Unauthorized");
    } else {
        daemon_log(2, "empty response from translation server\n");

        response_dispatch_message(&request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
    }
}

static void
handle_translated_request(request &request, const TranslateResponse &response)
{
    /* copy the TranslateResponse just in case the cache item is
       freed before we send the final response */
    /* TODO: use cache_item_lock() instead */
    auto response2 = NewFromPool<TranslateResponse>(request.request->pool);
    *response2 = response;
    translate_response_copy(request.request->pool, response2, &response);
    request.translate.response = response2;

    handle_translated_request2(request, *response2);
}

extern const TranslateHandler handler_translate_handler;

/**
 * Install a fake #TranslateResponse.  This is sometimes necessary
 * when we don't have a "real" response (yet), because much of the
 * code in response.c dereferences the #TranslateResponse pointer.
 */
static void
install_error_response(request &request)
{
    static TranslateResponse error_response;
    error_response.status = (http_status_t)-1;

    request.translate.response = &error_response;
    request.translate.transformation = nullptr;
}

static const char *
uri_without_query_string(struct pool *pool, const char *uri)
{
    assert(pool != nullptr);
    assert(uri != nullptr);

    const char *qmark = strchr(uri, '?');
    if (qmark != nullptr)
        return p_strndup(pool, uri, qmark - uri);

    return uri;
}

static void
handler_translate_response(const TranslateResponse *response,
                           void *ctx)
{
    struct request &request = *(struct request *)ctx;

    if (response->protocol_version > translation_protocol_version)
        translation_protocol_version = response->protocol_version;

    /* just in case we error out before handle_translated_request()
       assigns the real response */
    install_error_response(request);

    if (!strref_is_null(&response->check)) {
        /* repeat request with CHECK set */

        if (++request.translate.checks > 4) {
            daemon_log(2, "got too many consecutive CHECK packets\n");
            response_dispatch_message(&request,
                                      HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                      "Internal server error");
            return;
        }

        request.translate.previous = response;
        request.translate.request.check = response->check;

        translate_cache(request.request->pool,
                        request.connection->instance->translate_cache,
                        &request.translate.request,
                        &handler_translate_handler, &request,
                        &request.async_ref);
        return;
    }

    if (!strref_is_null(&response->want_full_uri)) {
        /* repeat request with full URI */

        if (request.translate.want_full_uri) {
            daemon_log(2, "duplicate TRANSLATE_WANT_FULL_URI packet\n");
            response_dispatch_message(&request,
                                      HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                      "Internal server error");
            return;
        }

        request.translate.want_full_uri = true;

        /* echo the server's WANT_FULL_URI packet */
        request.translate.request.want_full_uri = response->want_full_uri;

        /* send the full URI this time */
        request.translate.request.uri =
            uri_without_query_string(request.request->pool,
                                     request.request->uri);

        /* undo the uri_parse() call (but leave the query_string) */

        strref_set_c(&request.uri.base, request.translate.request.uri);
        strref_clear(&request.uri.args);
        strref_clear(&request.uri.path_info);

        /* resend the modified request */

        translate_cache(request.request->pool,
                        request.connection->instance->translate_cache,
                        &request.translate.request,
                        &handler_translate_handler, &request,
                        &request.async_ref);
        return;
    }

    if (response->previous) {
        if (request.translate.previous == nullptr) {
            daemon_log(2, "no previous translation response\n");
            response_dispatch_message(&request,
                                      HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                      "Internal server error");
            return;
        }

        /* apply changes from this response, then resume the
           "previous" response */
        struct session *session =
            apply_translate_response_session(request, *response);
        if (session != nullptr)
            session_put(session);

        response = request.translate.previous;
    }

    handle_translated_request(request, *response);
}

static void
handler_translate_error(GError *error, void *ctx)
{
    struct request &request = *(struct request *)ctx;

    daemon_log(1, "translation error on '%s': %s\n",
               request.request->uri, error->message);

    install_error_response(request);

    /* pretend this error was generated by the translation client, so
       the HTTP client sees a 500 and not a 404 (if the translation
       server is not running) */
    if (error->domain != translate_quark()) {
        error->domain = translate_quark();
        error->code = 0;
    }

    response_dispatch_error(&request, error);
    g_error_free(error);
}

const TranslateHandler handler_translate_handler = {
    .response = handler_translate_response,
    .error = handler_translate_error,
};

static bool
request_uri_parse(request &request2, parsed_uri &dest)
{
    const http_server_request &request = *request2.request;

    if (!uri_path_verify_quick(request.uri) ||
        !uri_parse(&dest, request.uri)) {
        /* response_dispatch() assumes that we have a translation
           response, and will dereference it - at this point, the
           translation server hasn't been queried yet, so we just
           insert an empty response here */
        install_error_response(request2);

        response_dispatch_message(&request2, HTTP_STATUS_BAD_REQUEST,
                                  "Malformed URI");
        return false;
    }

    return true;
}

static void
fill_translate_request(TranslateRequest *t,
                       const struct http_server_request *request,
                       const struct parsed_uri *uri,
                       struct strmap *args)
{
    t->local_address = request->local_address;
    t->local_address_length = request->local_address_length;
    t->remote_host = request->remote_host_and_port;
    t->host = strmap_get(request->headers, "host");
    t->user_agent = strmap_get(request->headers, "user-agent");
    t->ua_class = t->user_agent != nullptr
        ? ua_classification_lookup(t->user_agent)
        : nullptr;
    t->accept_language = strmap_get(request->headers, "accept-language");
    t->authorization = strmap_get(request->headers, "authorization");
    t->uri = strref_dup(request->pool, &uri->base);
    t->args = args != nullptr
        ? args_format(request->pool, args,
                      nullptr, nullptr, nullptr, nullptr,
                      "translate")
        : nullptr;
    if (t->args != nullptr && *t->args == 0)
        t->args = nullptr;

    t->query_string = strref_is_empty(&uri->query)
        ? nullptr
        : strref_dup(request->pool, &uri->query);
    t->widget_type = nullptr;
    strref_null(&t->check);
    strref_null(&t->want_full_uri);
    t->error_document_status = (http_status_t)0;
}

static void
ask_translation_server(struct request *request2, struct tcache *tcache)
{
    request2->translate.previous = nullptr;
    request2->translate.checks = 0;
    request2->translate.want_full_uri = false;

    http_server_request &request = *request2->request;
    fill_translate_request(&request2->translate.request, request2->request,
                           &request2->uri, request2->args);
    translate_cache(request.pool, tcache, &request2->translate.request,
                    &handler_translate_handler, request2,
                    &request2->async_ref);
}

static void
serve_document_root_file(request &request2,
                         const struct config *config)
{
    http_server_request &request = *request2.request;

    struct parsed_uri *uri = &request2.uri;

    auto tr = NewFromPool<TranslateResponse>(request.pool);
    tr->Clear();
    request2.translate.response = tr;

    const char *index_file = nullptr;
    bool process;
    if (uri->base.data[uri->base.length - 1] == '/') {
        index_file = "index.html";
        process = true;
    } else {
        process = strref_ends_with_n(&uri->base, ".html", 5);
    }

    if (process) {
        transformation *transformation =
            NewFromPool<struct transformation>(request.pool);
        widget_view *view = NewFromPool<widget_view>(request.pool);
        widget_view_init(view);

        transformation->next = nullptr;
        transformation->type = transformation::TRANSFORMATION_PROCESS;

        view->transformation = transformation;

        tr->views = view;
    } else {
        widget_view *view = NewFromPool<widget_view>(request.pool);
        widget_view_init(view);

        tr->views = view;
        tr->transparent = true;
    }

    request2.translate.transformation = tr->views->transformation;

    file_address *fa = NewFromPool<file_address>(request.pool);
    file_address_init(fa, p_strncat(request.pool,
                                    config->document_root,
                                    strlen(config->document_root),
                                    uri->base.data,
                                    uri->base.length,
                                    index_file, (size_t)10,
                                    nullptr));

    tr->address.type = RESOURCE_ADDRESS_LOCAL;
    tr->address.u.file = fa;

    tr->request_header_forward = (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
        },
    };

    tr->response_header_forward = (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
        },
    };

    request2.resource_tag = tr->address.u.file->path;
    request2.processor_focus = process &&
        strmap_get_checked(request2.args, "focus") != nullptr;

    file_callback(&request2);
}

/*
 * async operation
 *
 */

static void
handler_abort(struct async_operation *ao)
{
    request &request2 = *ContainerCast(ao, request, operation);

    request_discard_body(&request2);

    /* forward the abort to the http_server library */
    async_abort(&request2.async_ref);
}

static const struct async_operation_class handler_operation = {
    .abort = handler_abort,
};

/*
 * constructor
 *
 */

void
handle_http_request(client_connection &connection,
                    http_server_request &request,
                    struct async_operation_ref *async_ref)
{
    struct request *request2 = NewFromPool<struct request>(request.pool);
    request2->connection = &connection;
    request2->request = &request;
    request2->product_token = nullptr;
#ifndef NO_DATE_HEADER
    request2->date = nullptr;
#endif

    request2->args = nullptr;
    request2->cookies = nullptr;
    session_id_clear(&request2->session_id);
    request2->send_session_cookie = false;
#ifdef DUMP_WIDGET_TREE
    request2->dump_widget_tree = nullptr;
#endif
    request2->body = http_server_request_has_body(&request)
        ? istream_hold_new(request.pool, request.body)
        : nullptr;
    request2->transformed = false;

    async_init(&request2->operation, &handler_operation);
    async_ref_set(async_ref, &request2->operation);

#ifndef NDEBUG
    request2->response_sent = false;
#endif

    if (!request_uri_parse(*request2, request2->uri))
        return;

    assert(!strref_is_empty(&request2->uri.base));
    assert(request2->uri.base.data[0] == '/');

    request_args_parse(request2);
    request_determine_session(request2);

    if (connection.instance->translate_cache == nullptr)
        serve_document_root_file(*request2, connection.config);
    else
        ask_translation_server(request2, connection.instance->translate_cache);
}

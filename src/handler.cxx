/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.hxx"
#include "config.hxx"
#include "bp_instance.hxx"
#include "file_not_found.hxx"
#include "file_enotdir.hxx"
#include "file_directory_index.hxx"
#include "file_handler.hxx"
#include "file_address.hxx"
#include "nfs_address.h"
#include "nfs_handler.h"
#include "request.hxx"
#include "connection.h"
#include "args.h"
#include "session.hxx"
#include "tcache.hxx"
#include "suffix_registry.hxx"
#include "growing-buffer.h"
#include "header-writer.h"
#include "strref-pool.h"
#include "dpool.h"
#include "pbuffer.hxx"
#include "http_server.hxx"
#include "http_quark.h"
#include "transformation.hxx"
#include "expiry.h"
#include "uri-edit.h"
#include "uri-escape.h"
#include "uri-verify.h"
#include "strutil.h"
#include "strmap.h"
#include "istream.h"
#include "translate_client.hxx"
#include "ua_classification.h"
#include "beng-proxy/translation.h"
#include "util/Cast.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <sys/stat.h>

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
    if (!response.session.IsNull() || response.user != nullptr ||
        response.language != nullptr ||
        response.views->transformation != nullptr)
        session = request_get_session(&request);
    else
        session = nullptr;

    if (!response.session.IsNull()) {
        if (response.session.IsEmpty()) {
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
    const struct resource_address &address = *request.translate.address;

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
         address.type == RESOURCE_ADDRESS_NONE &&
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

    request.resource_tag = resource_address_id(&address,
                                               request.request->pool);

    request.processor_focus = request.args != nullptr &&
        request_processor_enabled(&request) &&
        strmap_get(request.args, "focus") != nullptr;

    if (address.type == RESOURCE_ADDRESS_LOCAL) {
        if (address.u.file->delegate != nullptr)
            delegate_handler(request);
        else
            file_callback(&request);
#ifdef HAVE_LIBNFS
    } else if (address.type == RESOURCE_ADDRESS_NFS) {
        nfs_handler(&request);
#endif
    } else if (address.type == RESOURCE_ADDRESS_HTTP ||
               address.type == RESOURCE_ADDRESS_LHTTP ||
               resource_address_is_cgi_alike(&address) ||
               address.type == RESOURCE_ADDRESS_NFS ||
               address.type == RESOURCE_ADDRESS_AJP) {
        proxy_handler(request);
    } else if (response.redirect != nullptr) {
        http_status_t status = response.status != (http_status_t)0
            ? response.status : HTTP_STATUS_SEE_OTHER;

        const char *uri = response.redirect;
        if (response.redirect_query_string && request.uri.query.length > 0)
            uri = uri_append_query_string_n(request.request->pool, uri,
                                            request.uri.query.data,
                                            request.uri.query.length);

        response_dispatch_redirect(&request, status, uri,
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

gcc_pure
static const char *
get_suffix(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash != nullptr)
        path = slash + 1;

    while (*path == '.')
        ++path;

    const char *dot = strrchr(path, '.');
    if (dot == nullptr || dot[1] == 0)
        return nullptr;

    return dot + 1;
}

gcc_pure
static const char *
get_suffix(const resource_address &address)
{
    switch (address.type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_LHTTP:
    case RESOURCE_ADDRESS_AJP:
    case RESOURCE_ADDRESS_PIPE:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
        return nullptr;

    case RESOURCE_ADDRESS_LOCAL:
        return get_suffix(address.u.file->path);

    case RESOURCE_ADDRESS_NFS:
        return get_suffix(address.u.nfs->path);
    }

    assert(false);
    gcc_unreachable();
}

static void
handler_suffix_registry_success(const char *content_type, void *ctx)
{
    struct request &request = *(struct request *)ctx;

    request.translate.content_type = content_type;
    handle_translated_request2(request, *request.translate.response);
}

static void
handler_suffix_registry_error(GError *error, void *ctx)
{
    struct request &request = *(struct request *)ctx;

    daemon_log(1, "translation error on '%s': %s\n",
               request.request->uri, error->message);

    response_dispatch_error(&request, error);
    g_error_free(error);
}

static constexpr SuffixRegistryHandler handler_suffix_registry_handler = {
    .success = handler_suffix_registry_success,
    .error = handler_suffix_registry_error,
};

static bool
do_content_type_lookup(request &request, const TranslateResponse &response)
{
    if (response.content_type_lookup.IsNull())
        return false;

    const char *suffix = get_suffix(*request.translate.address);
    if (suffix == nullptr)
        return false;

    const size_t length = strlen(suffix);
    if (length > 5)
        return false;

    /* duplicate the suffix, convert to lower case, check for
       "illegal" characters (non-alphanumeric) */
    char *buffer = p_strdup(request.request->pool, suffix);
    for (char *p = buffer; *p != 0; ++p) {
        const char ch = *p;
        if (char_is_capital_letter(ch))
            /* convert to lower case */
            *p += 'a' - 'A';
        else if (!char_is_minuscule_letter(ch) && !char_is_digit(ch))
            /* no, we won't look this up */
            return false;
    }

    suffix_registry_lookup(request.request->pool,
                           *request.connection->instance->translate_cache,
                           response.content_type_lookup, buffer,
                           handler_suffix_registry_handler, &request,
                           &request.async_ref);
    return true;
}

static void
handle_translated_request(request &request, const TranslateResponse &response)
{
    /* copy the TranslateResponse just in case the cache item is
       freed before we send the final response */
    /* TODO: use cache_item_lock() instead */
    auto response2 = NewFromPool<TranslateResponse>(request.request->pool);
    *response2 = response;
    response2->CopyFrom(request.request->pool, response);

    /* copy TRANSLATE_SESSION because TranslateResponse::CopyFrom()
       clears it */
    response2->session = DupBuffer(request.request->pool, response.session);
    response2->user = p_strdup_checked(request.request->pool, response.user);

    request.translate.response = response2;
    request.translate.address = &response2->address;

    apply_file_enotdir(request);

    if (!do_content_type_lookup(request, *response2))
        handle_translated_request2(request, *response2);
}

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
    request.translate.address = &error_response.address;
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
fill_translate_request_local_address(TranslateRequest &t,
                                     const http_server_request &r)
{
    t.local_address = r.local_address;
    t.local_address_length = r.local_address_length;
}

static void
fill_translate_request_remote_host(TranslateRequest &t,
                                   const http_server_request &r)
{
    t.remote_host = r.remote_host_and_port;
}

static void
fill_translate_request_user_agent(TranslateRequest &t,
                                  const strmap *headers)
{
    t.user_agent = strmap_get(headers, "user-agent");
}

static void
fill_translate_request_ua_class(TranslateRequest &t,
                                const strmap *headers)
{
    const char *user_agent = strmap_get(headers, "user-agent");

    t.ua_class = user_agent != nullptr
        ? ua_classification_lookup(user_agent)
        : nullptr;
}

static void
fill_translate_request_language(TranslateRequest &t,
                                const strmap *headers)
{
    t.accept_language = strmap_get(headers, "accept-language");
}

static void
fill_translate_request_args(TranslateRequest &t,
                            struct pool *pool, strmap *args)
{
    t.args = args != nullptr
        ? args_format(pool, args,
                      nullptr, nullptr, nullptr, nullptr,
                      "translate")
        : nullptr;
    if (t.args != nullptr && *t.args == 0)
        t.args = nullptr;
}

static void
fill_translate_request_query_string(TranslateRequest &t,
                                    struct pool *pool,
                                    const parsed_uri &uri)
{
    t.query_string = strref_is_empty(&uri.query)
        ? nullptr
        : strref_dup(pool, &uri.query);
}

static void
repeat_translation(struct request &request, const TranslateResponse &response)
{
    if (!response.check.IsNull()) {
        /* repeat request with CHECK set */

        if (++request.translate.n_checks > 4) {
            daemon_log(2, "got too many consecutive CHECK packets\n");
            response_dispatch_message(&request,
                                      HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                      "Internal server error");
            return;
        }

        request.translate.previous = &response;
        request.translate.request.check = response.check;
    }

    if (response.protocol_version >= 1) {
        /* handle WANT */

        request.translate.request.want = response.want;

        if (response.Wants(TRANSLATE_LOCAL_ADDRESS))
            fill_translate_request_local_address(request.translate.request,
                                                 *request.request);

        if (response.Wants(TRANSLATE_REMOTE_HOST))
            fill_translate_request_remote_host(request.translate.request,
                                               *request.request);

        if (response.Wants(TRANSLATE_USER_AGENT))
            fill_translate_request_user_agent(request.translate.request,
                                              request.request->headers);

        if (response.Wants(TRANSLATE_UA_CLASS))
            fill_translate_request_ua_class(request.translate.request,
                                            request.request->headers);

        if (response.Wants(TRANSLATE_LANGUAGE))
            fill_translate_request_language(request.translate.request,
                                            request.request->headers);

        if (response.Wants(TRANSLATE_ARGS) &&
            request.translate.request.args == nullptr)
            fill_translate_request_args(request.translate.request,
                                        request.request->pool, request.args);

        if (response.Wants(TRANSLATE_QUERY_STRING))
            fill_translate_request_query_string(request.translate.request,
                                                request.request->pool,
                                                request.uri);
    }

    if (!response.want_full_uri.IsNull()) {
        /* repeat request with full URI */

        /* echo the server's WANT_FULL_URI packet */
        request.translate.request.want_full_uri = response.want_full_uri;

        /* send the full URI this time */
        request.translate.request.uri =
            uri_without_query_string(request.request->pool,
                                     request.request->uri);

        /* undo the uri_parse() call (but leave the query_string) */

        strref_set_c(&request.uri.base, request.translate.request.uri);
        strref_clear(&request.uri.args);
        strref_clear(&request.uri.path_info);
    }

    /* resend the modified request */

    request.SubmitTranslateRequest();
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

    if (!response->check.IsNull() ||
        !response->want.IsEmpty() ||
        !response->want_full_uri.IsNull()) {
        repeat_translation(request, *response);
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

    /* check ENOTDIR */
    if (!response->enotdir.IsNull() && !check_file_enotdir(request, *response))
        return;

    /* check if the file exists */
    if (!response->file_not_found.IsNull() &&
        !check_file_not_found(request, *response))
        return;

    /* check if it's a directory */
    if (!response->directory_index.IsNull() &&
        !check_directory_index(request, *response))
        return;

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
    if (error->domain != translate_quark() &&
        error->domain != http_response_quark()) {
        error->domain = translate_quark();
        error->code = 0;
    }

    response_dispatch_error(&request, error);
    g_error_free(error);
}

static constexpr TranslateHandler handler_translate_handler = {
    .response = handler_translate_response,
    .error = handler_translate_error,
};

void
request::SubmitTranslateRequest()
{
    translate_cache(request->pool,
                    connection->instance->translate_cache,
                    &translate.request,
                    &handler_translate_handler, this,
                    &async_ref);
}

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
    /* these two were set by request_args_parse() */
    const auto session = t->session;
    const auto param = t->param;

    t->Clear();

    /* restore */
    t->session = session;
    t->param = param;

    t->host = strmap_get(request->headers, "host");
    t->authorization = strmap_get(request->headers, "authorization");
    t->uri = strref_dup(request->pool, &uri->base);

    if (translation_protocol_version < 1) {
        /* old translation server: send all packets that have become
           optional */
        fill_translate_request_local_address(*t, *request);
        fill_translate_request_remote_host(*t, *request);
        fill_translate_request_user_agent(*t, request->headers);
        fill_translate_request_ua_class(*t, request->headers);
        fill_translate_request_language(*t, request->headers);
        fill_translate_request_args(*t, request->pool, args);
        fill_translate_request_query_string(*t, request->pool, *uri);
    }
}

static void
ask_translation_server(struct request *request2)
{
    request2->translate.previous = nullptr;
    request2->translate.n_checks = 0;
    request2->translate.n_file_not_found = 0;
    request2->translate.n_directory_index = 0;
    request2->translate.enotdir_uri = nullptr;
    request2->translate.enotdir_path_info = nullptr;

    fill_translate_request(&request2->translate.request, request2->request,
                           &request2->uri, request2->args);
    request2->SubmitTranslateRequest();
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

    request2.translate.address = &tr->address;

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

    request2.resource_tag = request2.translate.address->u.file->path;
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
    request2->translate.content_type = nullptr;
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
        ask_translation_server(request2);
}

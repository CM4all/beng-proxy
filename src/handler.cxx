/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.hxx"
#include "bp_config.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "load_file.hxx"
#include "file_not_found.hxx"
#include "file_enotdir.hxx"
#include "file_directory_index.hxx"
#include "file_handler.hxx"
#include "file_address.hxx"
#include "nfs_address.hxx"
#include "nfs_handler.hxx"
#include "request.hxx"
#include "args.hxx"
#include "session.hxx"
#include "tcache.hxx"
#include "suffix_registry.hxx"
#include "address_suffix_registry.hxx"
#include "header_writer.hxx"
#include "pbuffer.hxx"
#include "http_headers.hxx"
#include "http_server/Request.hxx"
#include "http_quark.h"
#include "transformation.hxx"
#include "puri_edit.hxx"
#include "puri_escape.hxx"
#include "uri/uri_verify.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"
#include "TranslateHandler.hxx"
#include "translate_quark.hxx"
#include "ua_classification.hxx"
#include "beng-proxy/translation.h"
#include "util/Cast.hxx"
#include "util/CharUtil.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <sys/stat.h>

static unsigned translation_protocol_version;
static bool translation_protocol_version_received = false;

static const char *
bounce_uri(struct pool &pool, const Request &request,
           const TranslateResponse &response)
{
    const char *scheme = response.scheme != nullptr
        ? response.scheme : "http";
    const char *host = response.host != nullptr
        ? response.host
        : request.request.headers.Get("host");
    if (host == nullptr)
        host = "localhost";

    const char *uri_path = response.uri != nullptr
        ? p_strncat(&pool, response.uri, strlen(response.uri),
                    ";", request.uri.args.IsEmpty() ? (size_t)0 : 1,
                    request.uri.args.data, request.uri.args.size,
                    "?", request.uri.query.IsEmpty() ? (size_t)0 : 1,
                    request.uri.query.data, request.uri.query.size,
                    nullptr)
        : request.request.uri;

    const char *current_uri = p_strcat(&pool, scheme, "://", host, uri_path,
                                       nullptr);
    const char *escaped_uri = uri_escape_dup(&pool, current_uri,
                                             strlen(current_uri));

    return p_strcat(&pool, response.bounce, escaped_uri, nullptr);
}

/**
 * Apply session-specific data from the #TranslateResponse.  Returns
 * the session object or nullptr.
 */
static RealmSessionLease
apply_translate_response_session(Request &request,
                                 const TranslateResponse &response)
{
    request.ApplyTranslateRealm(response, nullptr);

    if (response.transparent) {
        request.MakeStateless();
        request.args = nullptr;
    } else if (response.discard_session)
        request.DiscardSession();

    return request.ApplyTranslateSession(response);
}

void
Request::HandleAddress(const ResourceAddress &address)
{
    assert(address.IsDefined());

    switch (address.type) {
    case ResourceAddress::Type::LOCAL:
        if (address.GetFile().delegate != nullptr)
            delegate_handler(*this, *address.GetFile().delegate,
                             address.GetFile().path);
        else
            file_callback(*this, address.GetFile());
        break;

#ifdef HAVE_LIBNFS
    case ResourceAddress::Type::NFS:
        nfs_handler(*this);
        break;
#endif

    default:
        proxy_handler(*this);
    }
}

/**
 * Called by handle_translated_request() with the #TranslateResponse
 * copy.
 */
static void
handle_translated_request2(Request &request,
                           const TranslateResponse &response)
{
    const auto &address = *request.translate.address;

    request.translate.transformation = response.views != nullptr
        ? response.views->transformation
        : nullptr;

    if ((response.request_header_forward.modes[HEADER_GROUP_COOKIE] != HEADER_FORWARD_MANGLE &&
         response.request_header_forward.modes[HEADER_GROUP_COOKIE] != HEADER_FORWARD_BOTH) ||
        (response.response_header_forward.modes[HEADER_GROUP_COOKIE] != HEADER_FORWARD_MANGLE &&
         response.response_header_forward.modes[HEADER_GROUP_COOKIE] != HEADER_FORWARD_BOTH)) {
        /* disable session management if cookies are not mangled by
           beng-proxy */
        request.MakeStateless();
    }

    if (response.site != nullptr)
        request.connection.site_name = response.site;

    {
        auto session = apply_translate_response_session(request, response);

        /* always enforce sessions when the processor is enabled */
        if (request.IsProcessorEnabled() && !session)
            session = request.MakeRealmSession();
    }

    request.resource_tag = address.GetId(request.pool);

    request.processor_focus = request.args != nullptr &&
        /* the IsProcessorEnabled() check was disabled because the
           response may include a X-CM4all-View header that enables
           the processor; with this check, the request body would be
           consumed already */
        //request.IsProcessorEnabled() &&
        request.args->Get("focus") != nullptr;

    if (address.IsDefined()) {
        request.HandleAddress(address);
    } else if (request.CheckHandleRedirectBounceStatus(response)) {
        /* done */
    } else if (response.www_authenticate != nullptr) {
        response_dispatch_message(request, HTTP_STATUS_UNAUTHORIZED,
                                  "Unauthorized");
    } else {
        response_dispatch_log(request, HTTP_STATUS_BAD_GATEWAY,
                              "Empty response from translation server");
    }
}

inline bool
Request::CheckHandleRedirect(const TranslateResponse &response)
{
    if (response.redirect == nullptr)
        return false;

    http_status_t status = response.status != (http_status_t)0
        ? response.status
        : HTTP_STATUS_SEE_OTHER;

    const char *redirect_uri = response.redirect;
    if (response.redirect_query_string && !uri.query.IsEmpty())
        redirect_uri = uri_append_query_string_n(&pool, redirect_uri,
                                                 uri.query);

    response_dispatch_redirect(*this, status, redirect_uri, nullptr);
    return true;
}

inline bool
Request::CheckHandleBounce(const TranslateResponse &response)
{
    if (response.bounce == nullptr)
        return false;

    response_dispatch_redirect(*this, HTTP_STATUS_SEE_OTHER,
                               bounce_uri(pool, *this,
                                          response),
                               nullptr);
    return true;
}

inline bool
Request::CheckHandleStatus(const TranslateResponse &response)
{
    if (response.status == (http_status_t)0)
        return false;

    response_dispatch(*this, response.status, HttpHeaders(), nullptr);
    return true;
}

bool
Request::CheckHandleRedirectBounceStatus(const TranslateResponse &response)
{
    return CheckHandleRedirect(response) ||
        CheckHandleBounce(response) ||
        CheckHandleStatus(response);
}

gcc_pure
static bool
ProbeOnePathSuffix(const char *prefix, const char *suffix)
{
    const size_t prefix_length = strlen(prefix);
    const size_t suffix_length = strlen(suffix);

    char path[PATH_MAX];
    if (prefix_length + suffix_length >= sizeof(path))
        /* path too long */
        return false;

    memcpy(path, prefix, prefix_length);
    memcpy(path + prefix_length, suffix, suffix_length);
    path[prefix_length + suffix_length] = 0;

    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

gcc_pure
static const char *
ProbePathSuffixes(const char *prefix, const ConstBuffer<const char *> suffixes)
{
    assert(!suffixes.IsNull());
    assert(!suffixes.IsEmpty());

    for (const char *current_suffix : suffixes) {
        if (ProbeOnePathSuffix(prefix, current_suffix))
            return current_suffix;
    }

    return nullptr;
}

inline bool
Request::CheckHandleProbePathSuffixes(const TranslateResponse &response)
{
    if (response.probe_path_suffixes.IsNull())
        return false;

    if (++translate.n_probe_path_suffixes > 2) {
        response_dispatch_log(*this, HTTP_STATUS_BAD_GATEWAY,
                              "Too many consecutive PROBE_PATH_SUFFIXES packets");
        return true;
    }

    assert(response.test_path != nullptr);
    const char *prefix = response.test_path;

    ConstBuffer<const char *> probe_suffixes(&response.probe_suffixes.front(),
                                             response.probe_suffixes.size());
    const char *found = ProbePathSuffixes(prefix, probe_suffixes);

    translate.request.probe_path_suffixes = response.probe_path_suffixes;
    translate.request.probe_suffix = found;
    SubmitTranslateRequest();
    return true;
}

static void
handler_suffix_registry_success(const char *content_type,
                                const Transformation *transformations,
                                void *ctx)
{
    auto &request = *(Request *)ctx;

    request.translate.content_type = content_type;
    request.translate.suffix_transformation = transformations;

    handle_translated_request2(request, *request.translate.response);
}

static void
handler_suffix_registry_error(GError *error, void *ctx)
{
    auto &request = *(Request *)ctx;

    daemon_log(1, "translation error on '%s': %s\n",
               request.request.uri, error->message);

    response_dispatch_error(request, error);
    g_error_free(error);
}

static constexpr SuffixRegistryHandler handler_suffix_registry_handler = {
    .success = handler_suffix_registry_success,
    .error = handler_suffix_registry_error,
};

static bool
do_content_type_lookup(Request &request,
                       const ResourceAddress &address)
{
    return suffix_registry_lookup(request.pool,
                                  *request.instance.translate_cache,
                                  address,
                                  handler_suffix_registry_handler, &request,
                                  request.async_ref);
}

static void
handle_translated_request(Request &request, const TranslateResponse &response)
{
    request.translate.response = &response;
    request.translate.address = &response.address;
    request.translate.transformation = nullptr;

    apply_file_enotdir(request);

    if (!do_content_type_lookup(request, response.address)) {
        request.translate.suffix_transformation = nullptr;
        handle_translated_request2(request, response);
    }
}

/**
 * Install a fake #TranslateResponse.  This is sometimes necessary
 * when we don't have a "real" response (yet), because much of the
 * code in response.c dereferences the #TranslateResponse pointer.
 */
static void
install_error_response(Request &request)
{
    static TranslateResponse error_response;
    error_response.status = (http_status_t)-1;

    request.translate.response = &error_response;
    request.translate.address = &error_response.address;
    request.translate.transformation = nullptr;
    request.translate.suffix_transformation = nullptr;
}

static const char *
uri_without_query_string(struct pool &pool, const char *uri)
{
    assert(uri != nullptr);

    const char *qmark = strchr(uri, '?');
    if (qmark != nullptr)
        return p_strndup(&pool, uri, qmark - uri);

    return uri;
}

static void
fill_translate_request_listener_tag(TranslateRequest &t,
                                    const Request &r)
{
    t.listener_tag = r.connection.listener_tag;
}

static void
fill_translate_request_local_address(TranslateRequest &t,
                                     const HttpServerRequest &r)
{
    t.local_address = r.local_address;
}

static void
fill_translate_request_remote_host(TranslateRequest &t,
                                   const HttpServerRequest &r)
{
    t.remote_host = r.remote_host_and_port;
}

static void
fill_translate_request_user_agent(TranslateRequest &t,
                                  const StringMap &headers)
{
    t.user_agent = headers.Get("user-agent");
}

static void
fill_translate_request_ua_class(TranslateRequest &t,
                                const StringMap &headers)
{
    const char *user_agent = headers.Get("user-agent");

    t.ua_class = user_agent != nullptr
        ? ua_classification_lookup(user_agent)
        : nullptr;
}

static void
fill_translate_request_language(TranslateRequest &t,
                                const StringMap &headers)
{
    t.accept_language = headers.Get("accept-language");
}

static void
fill_translate_request_args(TranslateRequest &t,
                            struct pool &pool, StringMap *args)
{
    t.args = args != nullptr
        ? args_format(&pool, args,
                      nullptr, nullptr, nullptr, nullptr,
                      "translate")
        : nullptr;
    if (t.args != nullptr && *t.args == 0)
        t.args = nullptr;
}

static void
fill_translate_request_query_string(TranslateRequest &t,
                                    struct pool &pool,
                                    const parsed_uri &uri)
{
    t.query_string = uri.query.IsEmpty()
        ? nullptr
        : p_strdup(pool, uri.query);
}

static void
fill_translate_request_user(Request &request,
                            TranslateRequest &t,
                            struct pool &pool)
{
    auto session = request.GetRealmSession();
    if (session) {
        if (session->user != nullptr)
            t.user = p_strdup(&pool, session->user);
    }
}

static void
repeat_translation(Request &request, const TranslateResponse &response)
{
    if (!response.check.IsNull()) {
        /* repeat request with CHECK set */

        if (++request.translate.n_checks > 4) {
            response_dispatch_log(request, HTTP_STATUS_BAD_GATEWAY,
                                  "Too many consecutive CHECK packets");
            return;
        }

        request.translate.previous = &response;
        request.translate.request.check = response.check;
    }

    if (!response.internal_redirect.IsNull()) {
        /* repeat request with INTERNAL_REDIRECT set */

        assert(response.want_full_uri.IsNull());

        if (++request.translate.n_internal_redirects > 4) {
            response_dispatch_log(request, HTTP_STATUS_BAD_GATEWAY,
                                  "Too many consecutive INTERNAL_REDIRECT packets");
            return;
        }

        request.translate.previous = &response;
        request.translate.request.internal_redirect = response.internal_redirect;

        assert(response.uri != nullptr);
        request.translate.request.uri = response.uri;
    }

    if (response.protocol_version >= 1) {
        /* handle WANT */

        if (!response.want.IsNull())
            request.translate.request.want = response.want;

        if (response.Wants(TRANSLATE_LISTENER_TAG)) {
            if (response.protocol_version >= 2) {
                response_dispatch_log(request, HTTP_STATUS_BAD_GATEWAY,
                                      "Translation protocol 2 doesn't allow WANT/LISTENER_TAG");
                return;
            }

            fill_translate_request_listener_tag(request.translate.request,
                                                request);
        }

        if (response.Wants(TRANSLATE_LOCAL_ADDRESS))
            fill_translate_request_local_address(request.translate.request,
                                                 request.request);

        if (response.Wants(TRANSLATE_REMOTE_HOST))
            fill_translate_request_remote_host(request.translate.request,
                                               request.request);

        if (response.Wants(TRANSLATE_USER_AGENT))
            fill_translate_request_user_agent(request.translate.request,
                                              request.request.headers);

        if (response.Wants(TRANSLATE_UA_CLASS))
            fill_translate_request_ua_class(request.translate.request,
                                            request.request.headers);

        if (response.Wants(TRANSLATE_LANGUAGE))
            fill_translate_request_language(request.translate.request,
                                            request.request.headers);

        if (response.Wants(TRANSLATE_ARGS) &&
            request.translate.request.args == nullptr)
            fill_translate_request_args(request.translate.request,
                                        request.pool, request.args);

        if (response.Wants(TRANSLATE_QUERY_STRING))
            fill_translate_request_query_string(request.translate.request,
                                                request.pool,
                                                request.uri);

        if (response.Wants(TRANSLATE_QUERY_STRING))
            fill_translate_request_query_string(request.translate.request,
                                                request.pool,
                                                request.uri);

        if (response.Wants(TRANSLATE_USER) ||
            request.translate.want_user) {
            request.translate.want_user = true;
            fill_translate_request_user(request, request.translate.request,
                                        request.pool);
        }
    }

    if (!response.want_full_uri.IsNull()) {
        /* repeat request with full URI */

        /* echo the server's WANT_FULL_URI packet */
        request.translate.request.want_full_uri = response.want_full_uri;

        /* send the full URI this time */
        request.translate.request.uri =
            uri_without_query_string(request.pool,
                                     request.request.uri);

        /* undo the uri_parse() call (but leave the query_string) */

        request.uri.base = request.translate.request.uri;
        request.uri.args = nullptr;
        request.uri.path_info = nullptr;
    }

    /* resend the modified request */

    request.SubmitTranslateRequest();
}

inline void
Request::OnTranslateResponse(const TranslateResponse &response)
{
    if (!response.session.IsNull())
        /* must apply SESSION early so it gets used by
           repeat_translation() */
        translate.request.session = response.session;

    translation_protocol_version_received = true;
    if (response.protocol_version > translation_protocol_version)
        translation_protocol_version = response.protocol_version;

    /* just in case we error out before handle_translated_request()
       assigns the real response */
    install_error_response(*this);

    if (response.HasAuth())
        HandleAuth(response);
    else
        OnTranslateResponseAfterAuth(response);
}

void
Request::OnTranslateResponseAfterAuth(const TranslateResponse &response)
{
    if (!response.check.IsNull() ||
        !response.internal_redirect.IsNull() ||
        !response.want.IsEmpty() ||
        /* after successful new authentication, repeat the translation
           if the translation server wishes to know the user */
        (translate.want_user && translate.user_modified) ||
        !response.want_full_uri.IsNull()) {

        /* repeat translation due to want_user||user_modified only
           once */
        translate.user_modified = false;

        repeat_translation(*this, response);
        return;
    }

    /* the CHECK is done by now; don't carry the CHECK value on to
       further translation requests */
    translate.request.check = nullptr;
    /* also reset the counter so we don't trigger the endless
       recursion detection by the ENOTDIR chain */
    translate.n_checks = 0;
    translate.n_internal_redirects = 0;

    if (response.previous) {
        if (translate.previous == nullptr) {
            response_dispatch_log(*this, HTTP_STATUS_BAD_GATEWAY,
                                  "No previous translation response");
            return;
        }

        /* apply changes from this response, then resume the
           "previous" response */
        apply_translate_response_session(*this, response);

        OnTranslateResponse2(*translate.previous);
    } else
        OnTranslateResponse2(response);
}

void
Request::OnTranslateResponse2(const TranslateResponse &response)
{
    if (CheckHandleReadFile(response))
        return;

    if (CheckHandleProbePathSuffixes(response))
        return;

    /* check ENOTDIR */
    if (!response.enotdir.IsNull() && !check_file_enotdir(*this, response))
        return;

    /* check if the file exists */
    if (!response.file_not_found.IsNull() &&
        !check_file_not_found(*this, response))
        return;

    /* check if it's a directory */
    if (!response.directory_index.IsNull() &&
        !check_directory_index(*this, response))
        return;

    handle_translated_request(*this, response);
}

inline bool
Request::CheckHandleReadFile(const TranslateResponse &response)
{
    if (response.read_file == nullptr)
        return false;

    if (++translate.n_read_file > 2) {
        response_dispatch_log(*this, HTTP_STATUS_BAD_GATEWAY,
                              "Too many consecutive READ_FILE packets");
        return true;
    }

    auto contents = LoadFile(pool, response.read_file, 256, nullptr);
    if (contents.IsNull())
        /* special case: if the file does not exist, return an empty
           READ_FILE packet to the translation server */
        contents.data = "";

    translate.request.read_file = contents;
    SubmitTranslateRequest();
    return true;
}

static void
handler_translate_response(TranslateResponse &response, void *ctx)
{
    auto &request = *(Request *)ctx;

    request.OnTranslateResponse(response);
}

static void
handler_translate_error(GError *error, void *ctx)
{
    auto &request = *(Request *)ctx;

    daemon_log(1, "translation error on '%s': %s\n",
               request.request.uri, error->message);

    install_error_response(request);

    /* pretend this error was generated by the translation client, so
       the HTTP client sees a 500 and not a 404 (if the translation
       server is not running) */
    if (error->domain != translate_quark() &&
        error->domain != http_response_quark()) {
        error->domain = translate_quark();
        error->code = 0;
    }

    response_dispatch_error(request, error);
    g_error_free(error);
}

static constexpr TranslateHandler handler_translate_handler = {
    .response = handler_translate_response,
    .error = handler_translate_error,
};

void
Request::SubmitTranslateRequest()
{
    translate_cache(pool,
                    *instance.translate_cache,
                    translate.request,
                    handler_translate_handler, this,
                    async_ref);
}

static bool
request_uri_parse(Request &request2, parsed_uri &dest)
{
    const auto &request = request2.request;

    if (!uri_path_verify_quick(request.uri) ||
        !dest.Parse(request.uri)) {
        /* response_dispatch() assumes that we have a translation
           response, and will dereference it - at this point, the
           translation server hasn't been queried yet, so we just
           insert an empty response here */
        install_error_response(request2);

        /* enable the "stateless" flag because we're at a very early
           stage, before request_determine_session(), and the
           session-related attributes have not been initialized yet */
        request2.stateless = true;

        response_dispatch_message(request2, HTTP_STATUS_BAD_REQUEST,
                                  "Malformed URI");
        return false;
    }

    return true;
}

static void
fill_translate_request(TranslateRequest &t,
                       const HttpServerRequest &request,
                       const struct parsed_uri &uri,
                       StringMap *args,
                       const char *listener_tag)
{
    /* these two were set by ParseArgs() */
    const auto session = t.session;
    const auto param = t.param;

    t.Clear();

    /* restore */
    t.session = session;
    t.param = param;

    t.host = request.headers.Get("host");
    t.authorization = request.headers.Get("authorization");
    t.uri = p_strdup(*request.pool, uri.base);

    if (translation_protocol_version < 1) {
        /* old translation server: send all packets that have become
           optional */
        fill_translate_request_local_address(t, request);
        fill_translate_request_remote_host(t, request);
        fill_translate_request_user_agent(t, request.headers);
        fill_translate_request_ua_class(t, request.headers);
        fill_translate_request_language(t, request.headers);
        fill_translate_request_args(t, *request.pool, args);
        fill_translate_request_query_string(t, *request.pool, uri);
    }

    if (translation_protocol_version >= 2 ||
        !translation_protocol_version_received)
        t.listener_tag = listener_tag;
}

static void
ask_translation_server(Request &request2)
{
    request2.translate.previous = nullptr;
    request2.translate.n_checks = 0;
    request2.translate.n_internal_redirects = 0;
    request2.translate.n_file_not_found = 0;
    request2.translate.n_directory_index = 0;
    request2.translate.n_probe_path_suffixes = 0;
    request2.translate.n_read_file = 0;
    request2.translate.enotdir_uri = nullptr;
    request2.translate.enotdir_path_info = nullptr;

    fill_translate_request(request2.translate.request, request2.request,
                           request2.uri, request2.args,
                           request2.connection.listener_tag);
    request2.SubmitTranslateRequest();
}

static void
serve_document_root_file(Request &request2, const BpConfig &config)
{
    auto *uri = &request2.uri;

    auto tr = NewFromPool<TranslateResponse>(request2.pool);
    tr->Clear();
    request2.translate.response = tr;

    const char *index_file = nullptr;
    if (uri->base.back() == '/')
        index_file = "index.html";

    auto view = NewFromPool<WidgetView>(request2.pool);
    view->Init(nullptr);

    tr->views = view;
    tr->transparent = true;

    request2.translate.transformation = tr->views->transformation;
    request2.translate.suffix_transformation = nullptr;

    const char *path = p_strncat(&request2.pool,
                                 config.document_root,
                                 strlen(config.document_root),
                                 uri->base.data, uri->base.size,
                                 index_file, (size_t)10,
                                 nullptr);
    auto *fa = NewFromPool<FileAddress>(request2.pool, path);
    tr->address = *fa;

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

    request2.resource_tag = request2.translate.address->GetFile().path;

    file_callback(request2, *fa);
}

/*
 * constructor
 *
 */

void
handle_http_request(BpConnection &connection,
                    HttpServerRequest &request,
                    struct async_operation_ref &async_ref)
{
    auto *request2 = NewFromPool<Request>(*request.pool,
                                          connection.instance,
                                          connection, request);

    request2->body = request.HasBody()
        ? istream_hold_new(*request.pool, *request.body)
        : nullptr;

    async_ref.Set(request2->operation);

    if (!request_uri_parse(*request2, request2->uri))
        return;

    assert(!request2->uri.base.IsEmpty());
    assert(request2->uri.base.front() == '/');

    request2->ParseArgs();
    request2->DetermineSession();

    if (request2->instance.translate_cache == nullptr)
        serve_document_root_file(*request2, connection.config);
    else
        ask_translation_server(*request2);
}

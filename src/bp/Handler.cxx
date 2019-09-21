/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Handler.hxx"
#include "Connection.hxx"
#include "Config.hxx"
#include "Instance.hxx"
#include "load_file.hxx"
#include "file_not_found.hxx"
#include "file_enotdir.hxx"
#include "file_directory_index.hxx"
#include "file_address.hxx"
#include "nfs/Address.hxx"
#include "Request.hxx"
#include "args.hxx"
#include "session/Session.hxx"
#include "ExternalSession.hxx"
#include "suffix_registry.hxx"
#include "address_suffix_registry.hxx"
#include "http/HeaderWriter.hxx"
#include "pool/pbuffer.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "AllocatorPtr.hxx"
#include "puri_edit.hxx"
#include "puri_escape.hxx"
#include "uri/Verify.hxx"
#include "RedirectHttps.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"
#include "translation/Service.hxx"
#include "translation/Handler.hxx"
#include "translation/Transformation.hxx"
#include "translation/Protocol.hxx"
#include "ua_classification.hxx"
#include "util/Cast.hxx"
#include "util/CharUtil.hxx"

#include <assert.h>
#include <sys/stat.h>

static unsigned translation_protocol_version;
static bool translation_protocol_version_received = false;

static const char *
GetBounceUri(struct pool &pool, const IncomingHttpRequest &request,
             const DissectedUri &dissected_uri,
             const TranslateResponse &response) noexcept
{
    const char *scheme = response.scheme != nullptr
        ? response.scheme : "http";
    const char *host = response.host != nullptr
        ? response.host
        : request.headers.Get("host");
    if (host == nullptr)
        host = "localhost";

    const char *uri_path = response.uri != nullptr
        ? p_strncat(&pool, response.uri, strlen(response.uri),
                    ";", dissected_uri.args == nullptr ? (size_t)0 : 1,
                    dissected_uri.args.data, dissected_uri.args.size,
                    "?", dissected_uri.query == nullptr ? (size_t)0 : 1,
                    dissected_uri.query.data, dissected_uri.query.size,
                    nullptr)
        : request.uri;

    const char *current_uri = p_strcat(&pool, scheme, "://", host, uri_path,
                                       nullptr);
    const char *escaped_uri = uri_escape_dup(pool, current_uri,
                                             strlen(current_uri));

    return p_strcat(&pool, response.bounce, escaped_uri, nullptr);
}

/**
 * Apply session-specific data from the #TranslateResponse.  Returns
 * the session object or nullptr.
 */
RealmSessionLease
Request::ApplyTranslateResponseSession(const TranslateResponse &response) noexcept
{
    ApplyTranslateRealm(response, nullptr);

    if (response.transparent) {
        MakeStateless();
        args.Clear();
    } else if (response.discard_session)
        DiscardSession();

    return ApplyTranslateSession(response);
}

void
Request::HandleAddress(const ResourceAddress &address)
{
    assert(address.IsDefined());

    switch (address.type) {
    case ResourceAddress::Type::LOCAL:
        if (address.GetFile().delegate != nullptr)
            HandleDelegateAddress(*address.GetFile().delegate,
                                  address.GetFile().path);
        else
            HandleFileAddress(address.GetFile());
        break;

    case ResourceAddress::Type::NFS:
        HandleNfsAddress();
        break;

    default:
        HandleProxyAddress();
    }
}

void
Request::HandleTranslatedRequest2(const TranslateResponse &response) noexcept
{
    const ResourceAddress address(ShallowCopy(), translate.address);

    translate.transformation = response.views != nullptr
        ? response.views->transformation
        : nullptr;

    using namespace BengProxy;
    if ((response.request_header_forward[HeaderGroup::COOKIE] != HeaderForwardMode::MANGLE &&
         response.request_header_forward[HeaderGroup::COOKIE] != HeaderForwardMode::BOTH) ||
        (response.response_header_forward[HeaderGroup::COOKIE] != HeaderForwardMode::MANGLE &&
         response.response_header_forward[HeaderGroup::COOKIE] != HeaderForwardMode::BOTH)) {
        /* disable session management if cookies are not mangled by
           beng-proxy */
        MakeStateless();
    }

    if (response.site != nullptr)
        connection.per_request.site_name = response.site;

    {
        auto session = ApplyTranslateResponseSession(response);

        /* always enforce sessions when the processor is enabled */
        if (IsProcessorEnabled() && !session)
            session = MakeRealmSession();

        if (session)
            RefreshExternalSession(connection.instance,
                                   session->parent);
    }

    resource_tag = address.GetId(pool);

    processor_focus =
        /* the IsProcessorEnabled() check was disabled because the
           response may include a X-CM4all-View header that enables
           the processor; with this check, the request body would be
           consumed already */
        //IsProcessorEnabled() &&
        args.Get("focus") != nullptr;

    if (address.IsDefined()) {
        HandleAddress(address);
    } else if (CheckHandleRedirectBounceStatus(response)) {
        /* done */
    } else if (response.www_authenticate != nullptr) {
        DispatchResponse(HTTP_STATUS_UNAUTHORIZED, "Unauthorized");
    } else {
        LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                         "Empty response from translation server", 1);
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

    if (response.redirect_full_uri && !dissected_uri.args.IsNull())
        redirect_uri = p_strncat(&pool, redirect_uri, strlen(redirect_uri),
                                 ";", size_t(1),
                                 dissected_uri.args.data, dissected_uri.args.size,
                                 dissected_uri.path_info.data, dissected_uri.path_info.size,
                                 nullptr);

    if (response.redirect_query_string && !dissected_uri.query.IsNull())
        redirect_uri = uri_append_query_string_n(&pool, redirect_uri,
                                                 dissected_uri.query);

    DispatchRedirect(status, redirect_uri, response.message);
    return true;
}

inline bool
Request::CheckHandleBounce(const TranslateResponse &response)
{
    if (response.bounce == nullptr)
        return false;

    DispatchRedirect(HTTP_STATUS_SEE_OTHER,
                     GetBounceUri(pool, request, dissected_uri, response),
                     nullptr);
    return true;
}

inline bool
Request::CheckHandleStatus(const TranslateResponse &response)
{
    if (response.status == (http_status_t)0)
        return false;

    DispatchResponse(response.status, HttpHeaders(pool), nullptr);
    return true;
}

inline bool
Request::CheckHandleMessage(const TranslateResponse &response)
{
    if (response.message == nullptr)
        return false;

    http_status_t status = response.status != (http_status_t)0
        ? response.status
        : HTTP_STATUS_OK;

    DispatchResponse(status, response.message);
    return true;
}

bool
Request::CheckHandleRedirectBounceStatus(const TranslateResponse &response)
{
    return CheckHandleRedirect(response) ||
        CheckHandleBounce(response) ||
        CheckHandleMessage(response) ||
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
    assert(!suffixes.empty());

    for (const char *current_suffix : suffixes) {
        if (ProbeOnePathSuffix(prefix, current_suffix))
            return current_suffix;
    }

    return nullptr;
}

inline bool
Request::CheckHandleProbePathSuffixes(const TranslateResponse &response)
{
    if (response.probe_path_suffixes == nullptr)
        return false;

    if (++translate.n_probe_path_suffixes > 2) {
        LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                         "Too many consecutive PROBE_PATH_SUFFIXES packets",
                         1);
        return true;
    }

    assert(response.test_path != nullptr);
    const char *prefix = response.test_path;

    const char *found = ProbePathSuffixes(prefix, response.probe_suffixes);

    translate.request.probe_path_suffixes = response.probe_path_suffixes;
    translate.request.probe_suffix = found;
    SubmitTranslateRequest();
    return true;
}

void
Request::OnSuffixRegistrySuccess(const char *content_type,
                                 const Transformation *transformations)
{
    translate.content_type = content_type;
    translate.suffix_transformation = transformations;

    HandleTranslatedRequest2(*translate.response);
}

void
Request::OnSuffixRegistryError(std::exception_ptr ep)
{
    LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                     "Configuration server failed",
                     ep, 1);
}

bool
Request::DoContentTypeLookup(const ResourceAddress &address) noexcept
{
    return suffix_registry_lookup(pool,
                                  *instance.translation_service,
                                  address,
                                  *this, cancel_ptr);
}

void
Request::HandleTranslatedRequest(const TranslateResponse &response) noexcept
{
    translate.response = &response;
    translate.address = {ShallowCopy(), response.address};
    translate.transformation = nullptr;

    apply_file_enotdir(*this);

    if (!DoContentTypeLookup(response.address)) {
        translate.suffix_transformation = nullptr;
        HandleTranslatedRequest2(response);
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
    request.translate.address = {ShallowCopy(), error_response.address};
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
                                     const IncomingHttpRequest &r)
{
    t.local_address = r.local_address;
}

static void
fill_translate_request_remote_host(TranslateRequest &t,
                                   const char *remote_host_and_port)
{
    t.remote_host = remote_host_and_port;
}

static void
fill_translate_request_user_agent(TranslateRequest &t,
                                  const StringMap &headers)
{
    t.user_agent = headers.Get("user-agent");
}

static void
fill_translate_request_ua_class(TranslateRequest &t,
                                const BpInstance &instance,
                                const StringMap &headers)
{
    if (!instance.ua_classification)
        return;

    const char *user_agent = headers.Get("user-agent");

    t.ua_class = user_agent != nullptr
        ? instance.ua_classification->Lookup(user_agent)
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
                            struct pool &pool, const StringMap &args)
{
    t.args = args_format(pool, &args,
                         nullptr, nullptr, nullptr, nullptr,
                         "translate");
    if (t.args != nullptr && *t.args == 0)
        t.args = nullptr;
}

static void
fill_translate_request_query_string(TranslateRequest &t,
                                    struct pool &pool,
                                    const DissectedUri &uri)
{
    t.query_string = uri.query.empty()
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

inline void
Request::RepeatTranslation(const TranslateResponse &response) noexcept
{
    if (!response.check.IsNull()) {
        /* repeat request with CHECK set */

        if (++translate.n_checks > 4) {
            LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                             "Too many consecutive CHECK packets",
                             1);
            return;
        }

        translate.previous = &response;
        translate.request.check = response.check;
    }

    if (!response.internal_redirect.IsNull()) {
        /* repeat request with INTERNAL_REDIRECT set */

        assert(response.want_full_uri == nullptr);

        if (++translate.n_internal_redirects > 4) {
            LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                             "Too many consecutive INTERNAL_REDIRECT packets",
                             1);
            return;
        }

        translate.previous = &response;
        translate.request.internal_redirect = response.internal_redirect;

        assert(response.uri != nullptr);
        translate.request.uri = response.uri;
    }

    if (response.protocol_version >= 1) {
        /* handle WANT */

        if (!response.want.IsNull())
            translate.request.want = response.want;

        if (response.Wants(TranslationCommand::LISTENER_TAG)) {
            if (response.protocol_version >= 2) {
                LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                                 "Translation protocol 2 doesn't allow WANT/LISTENER_TAG",
                                 1);
                return;
            }

            fill_translate_request_listener_tag(translate.request, *this);
        }

        if (response.Wants(TranslationCommand::LOCAL_ADDRESS))
            fill_translate_request_local_address(translate.request, request);

        if (response.Wants(TranslationCommand::REMOTE_HOST))
            fill_translate_request_remote_host(translate.request,
                                               connection.remote_host_and_port);

        if (response.Wants(TranslationCommand::USER_AGENT))
            fill_translate_request_user_agent(translate.request,
                                              request.headers);

        if (response.Wants(TranslationCommand::UA_CLASS))
            fill_translate_request_ua_class(translate.request,
                                            instance,
                                            request.headers);

        if (response.Wants(TranslationCommand::LANGUAGE))
            fill_translate_request_language(translate.request,
                                            request.headers);

        if (response.Wants(TranslationCommand::ARGS) &&
            translate.request.args == nullptr)
            fill_translate_request_args(translate.request, pool, args);

        if (response.Wants(TranslationCommand::QUERY_STRING))
            fill_translate_request_query_string(translate.request, pool,
                                                dissected_uri);

        if (response.Wants(TranslationCommand::QUERY_STRING))
            fill_translate_request_query_string(translate.request, pool,
                                                dissected_uri);

        if (response.Wants(TranslationCommand::USER) || translate.want_user) {
            translate.want_user = true;
            fill_translate_request_user(*this, translate.request, pool);
        }
    }

    if (!response.want_full_uri.IsNull()) {
        /* repeat request with full URI */

        /* echo the server's WANT_FULL_URI packet */
        translate.request.want_full_uri = response.want_full_uri;

        /* send the full URI this time */
        translate.request.uri = uri_without_query_string(pool, request.uri);

        /* undo the uri_parse() call (but leave the query_string) */

        dissected_uri.base = translate.request.uri;
        dissected_uri.args = nullptr;
        dissected_uri.path_info = nullptr;
    }

    /* resend the modified request */

    SubmitTranslateRequest();
}

inline void
Request::OnTranslateResponse(const TranslateResponse &response)
{
    if (response.https_only != 0) {
        const char *https = request.headers.Get("x-cm4all-https");
        if (https == nullptr || strcmp(https, "on") != 0) {
            /* not encrypted: redirect to https:// */

            const char *host = request.headers.Get("host");
            if (host == nullptr) {
                DispatchResponse(HTTP_STATUS_BAD_REQUEST, "No Host header");
                return;
            }

            DispatchRedirect(HTTP_STATUS_MOVED_PERMANENTLY,
                             MakeHttpsRedirect(pool, host,
                                               response.https_only,
                                               request.uri),
                             "This page requires \"https\"");
            return;
        }
    }

    if (!response.session.IsNull())
        /* must apply SESSION early so it gets used by
           repeat_translation() */
        translate.request.session = response.session;

    translation_protocol_version_received = true;
    if (response.protocol_version > translation_protocol_version)
        translation_protocol_version = response.protocol_version;

    /* just in case we error out before HandleTranslatedRequest()
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
        !response.want.empty() ||
        /* after successful new authentication, repeat the translation
           if the translation server wishes to know the user */
        (translate.want_user && translate.user_modified) ||
        !response.want_full_uri.IsNull()) {

        /* repeat translation due to want_user||user_modified only
           once */
        translate.user_modified = false;

        RepeatTranslation(response);
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
            LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                             "No previous translation response", 1);
            return;
        }

        /* apply changes from this response, then resume the
           "previous" response */
        ApplyTranslateResponseSession(response);

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

    HandleTranslatedRequest(response);
}

inline bool
Request::CheckHandleReadFile(const TranslateResponse &response)
{
    if (response.read_file == nullptr)
        return false;

    if (++translate.n_read_file > 2) {
        LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                         "Too many consecutive READ_FILE packets", 1);
        return true;
    }

    ConstBuffer<void> contents;

    try {
        contents = LoadFile(pool, response.read_file, 256);
    } catch (...) {
        /* special case: if the file does not exist, return an empty
           READ_FILE packet to the translation server */
        contents.data = "";
        contents.size = 0;
    }

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
handler_translate_error(std::exception_ptr ep, void *ctx)
{
    auto &request = *(Request *)ctx;

    install_error_response(request);

    request.LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
                             "Configuration server failed", ep, 1);
}

static constexpr TranslateHandler handler_translate_handler = {
    .response = handler_translate_response,
    .error = handler_translate_error,
};

void
Request::SubmitTranslateRequest()
{
    instance.translation_service->SendRequest(pool,
                                              translate.request,
                                              handler_translate_handler, this,
                                              cancel_ptr);
}

bool
Request::ParseRequestUri() noexcept
{
    if (!uri_path_verify_quick(request.uri) ||
        !dissected_uri.Parse(request.uri)) {
        /* DispatchRedirect() assumes that we have a translation
           response, and will dereference it - at this point, the
           translation server hasn't been queried yet, so we just
           insert an empty response here */
        install_error_response(*this);

        /* enable the "stateless" flag because we're at a very early
           stage, before request_determine_session(), and the
           session-related attributes have not been initialized yet */
        stateless = true;

        DispatchResponse(HTTP_STATUS_BAD_REQUEST, "Malformed URI");
        return false;
    }

    return true;
}

static void
fill_translate_request(TranslateRequest &t,
                       const BpInstance &instance,
                       const IncomingHttpRequest &request,
                       const DissectedUri &uri,
                       const StringMap &args,
                       const char *listener_tag,
                       const char *remote_host_and_port)
{
    /* these two were set by ParseArgs() */
    const auto session = t.session;
    const auto param = t.param;

    /* restore */
    t.session = session;
    t.param = param;

    t.host = request.headers.Get("host");
    t.authorization = request.headers.Get("authorization");
    t.uri = p_strdup(request.pool, uri.base);

    if (translation_protocol_version < 1) {
        /* old translation server: send all packets that have become
           optional */
        fill_translate_request_local_address(t, request);
        fill_translate_request_remote_host(t, remote_host_and_port);
        fill_translate_request_user_agent(t, request.headers);
        fill_translate_request_ua_class(t, instance, request.headers);
        fill_translate_request_language(t, request.headers);
        fill_translate_request_args(t, request.pool, args);
        fill_translate_request_query_string(t, request.pool, uri);
    }

    if (translation_protocol_version >= 2 ||
        !translation_protocol_version_received)
        t.listener_tag = listener_tag;
}

inline void
Request::AskTranslationServer() noexcept
{
    translate.previous = nullptr;
    translate.n_checks = 0;
    translate.n_internal_redirects = 0;
    translate.n_file_not_found = 0;
    translate.n_directory_index = 0;
    translate.n_probe_path_suffixes = 0;
    translate.n_read_file = 0;
    translate.enotdir_uri = nullptr;
    translate.enotdir_path_info = nullptr;

    fill_translate_request(translate.request,
                           instance,
                           request,
                           dissected_uri, args,
                           connection.listener_tag,
                           connection.remote_host_and_port);
    SubmitTranslateRequest();
}

inline void
Request::ServeDocumentRootFile(const BpConfig &config) noexcept
{
    auto tr = NewFromPool<TranslateResponse>(pool);
    tr->Clear();
    translate.response = tr;

    const char *index_file = nullptr;
    if (dissected_uri.base.back() == '/')
        index_file = "index.html";

    auto view = NewFromPool<WidgetView>(pool, nullptr);

    tr->views = view;
    tr->transparent = true;

    translate.transformation = tr->views->transformation;
    translate.suffix_transformation = nullptr;

    const char *path = p_strncat(&pool,
                                 config.document_root,
                                 strlen(config.document_root),
                                 dissected_uri.base.data, dissected_uri.base.size,
                                 index_file, (size_t)10,
                                 nullptr);
    auto *fa = NewFromPool<FileAddress>(pool, path);
    tr->address = *fa;

    translate.address = {ShallowCopy(), tr->address};

    using namespace BengProxy;
    tr->request_header_forward = HeaderForwardSettings::MakeDefaultRequest();
    tr->response_header_forward = HeaderForwardSettings::MakeDefaultResponse();

    resource_tag = translate.address.GetFile().path;

    HandleFileAddress(*fa);
}

void
Request::HandleHttpRequest(CancellablePointer &caller_cancel_ptr) noexcept
{
    caller_cancel_ptr = *this;

    if (!ParseRequestUri())
        return;

    assert(!dissected_uri.base.empty());
    assert(dissected_uri.base.front() == '/');

    ParseArgs();
    DetermineSession();

    if (instance.translation_service == nullptr)
        ServeDocumentRootFile(connection.config);
    else
        AskTranslationServer();
}

void
handle_http_request(BpConnection &connection,
                    IncomingHttpRequest &request,
                    CancellablePointer &cancel_ptr)
{
    auto *request2 = NewFromPool<Request>(request.pool,
                                          connection, request);
    request2->HandleHttpRequest(cancel_ptr);
}

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

#ifndef BENG_PROXY_REQUEST_HXX
#define BENG_PROXY_REQUEST_HXX

#include "uri/Dissect.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "suffix_registry.hxx"
#include "delegate/Handler.hxx"
#include "nfs/Cache.hxx"
#include "strmap.hxx"
#include "penv.hxx"
#include "session/Session.hxx"
#include "widget/View.hxx"
#include "HttpResponseHandler.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"

#include <exception>

class FileDescriptor;
class Istream;
class HttpHeaders;
class GrowingBuffer;
struct BpInstance;
struct BpConfig;
struct BpConnection;
struct IncomingHttpRequest;
struct FilterTransformation;
struct DelegateAddress;

/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 */
struct Request final : HttpResponseHandler, DelegateHandler,
    NfsCacheHandler, SuffixRegistryHandler, Cancellable {

    struct pool &pool;

    BpInstance &instance;
    BpConnection &connection;

    const LLogger logger;

    IncomingHttpRequest &request;
    DissectedUri dissected_uri;

    StringMap args;

    StringMap *cookies = nullptr;

    /**
     * The name of the session cookie.
     */
    const char *session_cookie;

    SessionId session_id;
    StringBuffer<sizeof(SessionId) * 2 + 1> session_id_string;
    bool send_session_cookie = false;

    /**
     * The realm name of the request.  This is valid only after the
     * translation server has responded, because the translation
     * server may override it.
     *
     * This is set by ApplyTranslateRealm().  We initialize it here to
     * nullptr so ApplyTranslateRealm() can skip a second call when
     * it's already set.
     */
    const char *realm = nullptr;

    /**
     * Is this request "stateless", i.e. is session management
     * disabled?  This is initialized by request_determine_session(),
     * and may be disabled later by handle_translated_request().
     */
    bool stateless;

    struct {
        TranslateRequest request;
        const TranslateResponse *response;

        ResourceAddress address;

        /**
         * The next transformation.
         */
        const Transformation *transformation;

        /**
         * The next transformation from the
         * #TRANSLATE_CONTENT_TYPE_LOOKUP response.  These are applied
         * before other transformations.
         */
        const Transformation *suffix_transformation;

        /**
         * A pointer to the "previous" translate response, non-nullptr
         * only if beng-proxy sends a second translate request with a
         * CHECK packet.
         */
        const TranslateResponse *previous;

        /**
         * Number of CHECK packets followed so far.  This variable is
         * used for loop detection.
         */
        unsigned n_checks;

        unsigned n_internal_redirects;

        unsigned n_read_file;

        /**
         * Number of FILE_NOT_FOUND packets followed so far.  This
         * variable is used for loop detection.
         */
        unsigned n_file_not_found;

        /**
         * Number of #TRANSLATE_DIRECTORY_INDEX packets followed so
         * far.  This variable is used for loop detection.
         */
        unsigned n_directory_index;

        unsigned n_probe_path_suffixes;

        /**
         * The Content-Type returned by suffix_registry_lookup().
         */
        const char *content_type = nullptr;

        char *enotdir_uri;
        const char *enotdir_path_info;

        /**
         * Did we see #TRANSLATE_WANT with #TRANSLATE_USER?  If so,
         * and the user gets modified (see #user_modified), then we
         * need to repeat the initial translation with the new user
         * value.
         */
        bool want_user = false;

        /**
         * Did we receive #TRANSLATE_USER which modified the session's
         * "user" attribute?  If so, then we need to repeat the
         * initial translation with the new user value.
         */
        bool user_modified = false;
    } translate;

    /**
     * Area for handler-specific state variables.  This is a union to
     * save memory.
     */
    union {
        struct {
            const FileAddress *address;
        } file;

        struct {
            const char *path;
        } delegate;
    } handler;

    /**
     * The URI used for the cookie jar.  This is only used by
     * proxy_handler().
     */
    const char *cookie_uri;

    /**
     * The product token (RFC 2616 3.8) being forwarded; nullptr if
     * beng-proxy shall generate one.
     */
    const char *product_token = nullptr;

#ifndef NO_DATE_HEADER
    /**
     * The "date" response header (RFC 2616 14.18) being forwarded;
     * nullptr if beng-proxy shall generate one.
     */
    const char *date = nullptr;
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
    UnusedHoldIstreamPtr request_body;

    /**
     * This attribute remembers the previous status for
     * ApplyFilterStatus().  Zero means the response was not generated
     * by a filter.
     */
    http_status_t previous_status = http_status_t(0);

    /**
     * Shall the Set-Cookie2 header received from the next server be
     * evaluated?
     */
    bool collect_cookies = false;

    /**
     * Is the processor active, and is there a focused widget?
     */
    bool processor_focus;

    /**
     * Was the response already transformed?  The error document only
     * applies to the original, untransformed response.
     */
    bool transformed = false;

    /**
     * Is the pending response compressed?  This flag is used to avoid
     * compressing twice via #TRANSLATE_AUTO_GZIP and others.
     */
    bool compressed = false;

#ifndef NDEBUG
    bool response_sent = false;
#endif

    CancellablePointer cancel_ptr;

    Request(BpConnection &_connection,
            IncomingHttpRequest &_request) noexcept;

    void HandleHttpRequest(CancellablePointer &caller_cancel_ptr) noexcept;

    void ParseArgs();

    void RepeatTranslation(const TranslateResponse &response) noexcept;

    /**
     * Submit the #TranslateResponse to the translation cache.
     */
    void SubmitTranslateRequest();

    void AskTranslationServer() noexcept;
    void ServeDocumentRootFile(const BpConfig &config) noexcept;

    bool ParseRequestUri() noexcept;

    void OnTranslateResponse(const TranslateResponse &response);
    void OnTranslateResponseAfterAuth(const TranslateResponse &response);
    void OnTranslateResponse2(const TranslateResponse &response);

    /**
     * Enable the "stateless" flag, which disables session management
     * permanently for this request.
     */
    void MakeStateless() {
        session_id.Clear();
        stateless = true;
    }

    /**
     * Apply and verify #TRANSLATE_REALM.
     */
    void ApplyTranslateRealm(const TranslateResponse &response,
                             ConstBuffer<void> auth_base);

    /**
     * Copy the packets #TRANSLATE_SESSION, #TRANSLATE_USER,
     * #TRANSLATE_LANGUAGE from the #TranslateResponse to the
     * #session.
     *
     * @return the session
     */
    RealmSessionLease ApplyTranslateSession(const TranslateResponse &response);

    /**
     * Apply session-specific data from the #TranslateResponse.  Returns
     * the session object or nullptr.
     */
    RealmSessionLease ApplyTranslateResponseSession(const TranslateResponse &response) noexcept;

    bool CheckHandleReadFile(const TranslateResponse &response);
    bool CheckHandleProbePathSuffixes(const TranslateResponse &response);
    bool CheckHandleRedirect(const TranslateResponse &response);
    bool CheckHandleBounce(const TranslateResponse &response);
    bool CheckHandleStatus(const TranslateResponse &response);
    bool CheckHandleMessage(const TranslateResponse &response);
    bool CheckHandleRedirectBounceStatus(const TranslateResponse &response);

    bool DoContentTypeLookup(const ResourceAddress &address) noexcept;

    /**
     * Handle #TRANSLATE_AUTH.
     */
    void HandleAuth(const TranslateResponse &response);

    bool EvaluateFileRequest(FileDescriptor fd, const struct stat &st,
                             struct file_request &file_request) noexcept;

    void DispatchFile(const struct stat &st,
                      const struct file_request &file_request,
                      Istream *body) noexcept;

    bool DispatchCompressedFile(const struct stat &st,
                                Istream &body, const char *encoding,
                                const char *path);

    bool CheckCompressedFile(const struct stat &st,
                             Istream &body, const char *encoding,
                             const char *path) noexcept;

    bool CheckAutoCompressedFile(const struct stat &st,
                                 Istream &body, const char *encoding,
                                 const char *path,
                                 const char *suffix) noexcept;

    bool EmulateModAuthEasy(const FileAddress &address,
                            const struct stat &st, Istream *body) noexcept;

    bool MaybeEmulateModAuthEasy(const FileAddress &address,
                                 const struct stat &st, Istream *body) noexcept;

    void HandleFileAddress(const FileAddress &address) noexcept;

    void HandleDelegateAddress(const DelegateAddress &address,
                               const char *path) noexcept;

    void HandleNfsAddress() noexcept;

    /**
     * Handle the request by forwarding it to the given address.
     */
    void HandleProxyAddress() noexcept;

    /**
     * Handle the request by forwarding it to the given address.
     */
    void HandleAddress(const ResourceAddress &address);

    /**
     * Called by HandleTranslatedRequest() with the #TranslateResponse
     * copy.
     */
    void HandleTranslatedRequest2(const TranslateResponse &response) noexcept;

    void HandleTranslatedRequest(const TranslateResponse &response) noexcept;

    bool IsTransformationEnabled() const {
        return translate.response->views->transformation != nullptr;
    }

    /**
     * Returns true if the first transformation (if any) is the
     * processor.
     */
    bool IsProcessorFirst() const {
        return IsTransformationEnabled() &&
            translate.response->views->transformation->type
            == Transformation::Type::PROCESS;
    }

    bool IsProcessorEnabled() const;

    bool HasTransformations() const {
        return translate.transformation != nullptr ||
            translate.suffix_transformation != nullptr;
    }

    void CancelTransformations() {
        translate.transformation = nullptr;
        translate.suffix_transformation = nullptr;
    }

    const Transformation *PopTransformation() {
        const Transformation *t = translate.suffix_transformation;
        if (t != nullptr)
            translate.suffix_transformation = t->next;
        else {
            t = translate.transformation;
            if (t != nullptr)
                translate.transformation = t->next;
        }

        return t;
    }

    /**
     * Discard the request body if it was not used yet.  Call this
     * before sending the response to the HTTP server library.
     */
    void DiscardRequestBody() {
        request_body.Clear();
    }

private:
    const StringMap *GetCookies();
    const char *GetCookieSessionId();
    const char *GetUriSessionId();

    SessionLease LoadSession(const char *_session_id);

public:
    void DetermineSession();

    SessionLease GetSession() const {
        return SessionLease(session_id);
    }

    RealmSessionLease GetRealmSession() const;

    SessionLease MakeSession();
    RealmSessionLease MakeRealmSession();

    void IgnoreSession();
    void DiscardSession();

    const char *GetCookieURI() const {
        return cookie_uri;
    }

    const char *GetCookieHost() const;
    void CollectCookies(const StringMap &headers);

    void DispatchResponseDirect(http_status_t status, HttpHeaders &&headers,
                                UnusedIstreamPtr body);

    void DispatchResponse(http_status_t status, HttpHeaders &&headers,
                          UnusedIstreamPtr body);

    void DispatchResponse(http_status_t status, HttpHeaders &&headers,
                          std::nullptr_t) {
        DispatchResponse(status, std::move(headers), UnusedIstreamPtr());
    }

    void DispatchResponse(http_status_t status, const char *msg);

    void DispatchResponse(http_status_t status, HttpHeaders &&headers,
                          const char *msg);

    void DispatchRedirect(http_status_t status, const char *location,
                          const char *msg);

private:
    UnusedIstreamPtr AutoDeflate(HttpHeaders &response_headers,
                                 UnusedIstreamPtr response_body);

    void InvokeXmlProcessor(http_status_t status,
                            StringMap &response_headers,
                            UnusedIstreamPtr response_body,
                            const Transformation &transformation);

    void InvokeCssProcessor(http_status_t status,
                            StringMap &response_headers,
                            UnusedIstreamPtr response_body,
                            const Transformation &transformation);

    void InvokeTextProcessor(http_status_t status,
                             StringMap &response_headers,
                             UnusedIstreamPtr response_body);

    void InvokeSubst(http_status_t status,
                     StringMap &&response_headers,
                     UnusedIstreamPtr response_body,
                     bool alt_syntax,
                     const char *prefix,
                     const char *yaml_file,
                     const char *yaml_map_path) noexcept;

    void ApplyFilter(http_status_t status, StringMap &&headers2,
                     UnusedIstreamPtr body,
                     const FilterTransformation &filter) noexcept;

    void ApplyTransformation(http_status_t status, StringMap &&headers,
                             UnusedIstreamPtr response_body,
                             const Transformation &transformation);

    /**
     * Callback for forward_response_headers().
     */
    static const char *RelocateCallback(const char *const uri,
                                        void *ctx) noexcept;

    /**
     * Generate additional response headers as needed.
     */
    void MoreResponseHeaders(HttpHeaders &headers) const noexcept;

    /**
     * Generate the Set-Cookie response header for the given request.
     */
    void GenerateSetCookie(GrowingBuffer &headers);

public:
    void LogDispatchError(http_status_t status, const char *log_msg,
                          unsigned log_level=2);

    void LogDispatchError(http_status_t status, const char *msg,
                          const char *log_msg, unsigned log_level=2);

    void LogDispatchError(std::exception_ptr ep);

    void LogDispatchError(http_status_t status, const char *msg,
                          std::exception_ptr ep, unsigned log_level=2);

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        DiscardRequestBody();

        /* forward the abort to the http_server library */
        cancel_ptr.Cancel();
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        UnusedIstreamPtr body) noexcept override;
    void OnHttpError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class DelegateHandler */
    void OnDelegateSuccess(UniqueFileDescriptor fd) override;
    void OnDelegateError(std::exception_ptr ep) override;

    /* virtual methods from class NfsCacheHandler */
    void OnNfsCacheResponse(NfsCacheHandle &handle,
                            const struct stat &st) noexcept override;
    void OnNfsCacheError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class SuffixRegistryHandler */
    void OnSuffixRegistrySuccess(const char *content_type,
                                 const Transformation *transformations) override;
    void OnSuffixRegistryError(std::exception_ptr ep) override;
};

#endif

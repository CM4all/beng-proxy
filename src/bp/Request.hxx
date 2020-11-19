/*
 * Copyright 2007-2020 CM4all GmbH
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

#pragma once

#include "uri/Dissect.hxx"
#include "pool/LeakDetector.hxx"
#include "pool/SharedPtr.hxx"
#include "pool/UniquePtr.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "suffix_registry.hxx"
#include "delegate/Handler.hxx"
#include "nfs/Cache.hxx"
#include "strmap.hxx"
#include "session/Session.hxx"
#include "widget/View.hxx"
#include "HttpResponseHandler.hxx"
#include "io/Logger.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"

#ifdef HAVE_URING
#include "io/uring/Handler.hxx"
#endif

#include <exception>

struct statx;
class Istream;
class HttpHeaders;
class GrowingBuffer;
struct BpInstance;
struct BpConfig;
struct BpConnection;
struct IncomingHttpRequest;
struct PendingResponse;
struct FilterTransformation;
struct DelegateAddress;
struct WidgetContext;

/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 */
class Request final : public HttpResponseHandler, DelegateHandler,
		      TranslateHandler,
#ifdef HAVE_URING
		      Uring::OpenStatHandler,
#endif
#ifdef HAVE_LIBNFS
		      NfsCacheHandler,
#endif
		      SuffixRegistryHandler, Cancellable, PoolLeakDetector {

public:
	struct pool &pool;

	BpInstance &instance;
	BpConnection &connection;

private:
	const LLogger logger;

public:
	StopwatchPtr stopwatch;

	IncomingHttpRequest &request;

	DissectedUri dissected_uri;

	StringMap args;

private:
	StringMap *cookies = nullptr;

	/**
	 * The name of the session cookie.
	 */
	const char *session_cookie;

	SessionId session_id;
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
	 * The authenticated user, announced by the translation server
	 * in the #TranslationCommand::USER packet.  This pointer is
	 * allocated from this object's pool, and is usually a copy
	 * from the session.
	 */
	const char *user = nullptr;

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
		IntrusiveForwardList<Transformation> transformations;

		/**
		 * The next transformation from the
		 * #TRANSLATE_CONTENT_TYPE_LOOKUP response.  These are applied
		 * before other transformations.
		 */
		IntrusiveForwardList<Transformation> suffix_transformations;

		ConstBuffer<void> chain = nullptr;

		const char *chain_header = nullptr;

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

		unsigned n_chain = 0;

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
	struct { // TODO: convert this back to union
		struct {
			const FileAddress *address;

			UniqueFileDescriptor base_;

			FileDescriptor base;
		} file;

		struct {
			const char *path;
		} delegate;
	} handler;

	/**
	 * A filter with TranslationCommand::FILTER_NO_BODY is
         * running, and this response will be sent as soon as it
         * finishes successfully.
	 */
	UniquePoolPtr<PendingResponse> pending_filter_response;

	/**
         * This response is pending, waiting for the CHAIN translation
         * request to be finished, so we know where to send it.
	 */
	UniquePoolPtr<PendingResponse> pending_chain_response;

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

	SharedPoolPtr<WidgetContext> widget_context;

public:
	/**
	 * A pointer to the request body, or nullptr if there is none.  Once
	 * the request body has been "used", this pointer gets cleared.
	 */
	UnusedHoldIstreamPtr request_body;

private:
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

public:
	/**
	 * Is the processor active, and is there a focused widget?
	 */
	bool processor_focus;

private:
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

public:
	CancellablePointer cancel_ptr;

public:
	Request(BpConnection &_connection,
		IncomingHttpRequest &_request,
		const StopwatchPtr &parent_stopwatch) noexcept;

	~Request() noexcept;

private:
	void Destroy() noexcept {
		this->~Request();
	}

public:
	void HandleHttpRequest(CancellablePointer &caller_cancel_ptr) noexcept;

private:
	void ParseArgs();

	void RepeatTranslation(const TranslateResponse &response) noexcept;

public:
	/**
	 * Submit the #TranslateResponse to the translation cache.
	 */
	void SubmitTranslateRequest();

private:
	/**
	 * Install a fake #TranslateResponse.  This is sometimes necessary
	 * when we don't have a "real" response (yet), because much of the
	 * code in Response.cxx dereferences the #TranslateResponse
	 * pointer.
	 */
	void InstallErrorTranslateResponse() noexcept;

	void AskTranslationServer() noexcept;
	void ServeDocumentRootFile(const BpConfig &config) noexcept;

	bool ParseRequestUri() noexcept;

public:
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

	gcc_pure
	bool HasValidCsrfToken() noexcept;

	/**
	 * @return false if there is no valid token (an error response has
	 * already been sent then)
	 */
	bool CheckCsrfToken() noexcept;

	void WriteCsrfToken(HttpHeaders &headers) noexcept;

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

	bool CheckFileNotFound(const TranslateResponse &response) noexcept;

	bool CheckHandleReadFile(const TranslateResponse &response);
	bool CheckHandleProbePathSuffixes(const TranslateResponse &response);
	bool CheckHandleRedirect(const TranslateResponse &response);
	bool CheckHandleBounce(const TranslateResponse &response);
	bool CheckHandleStatus(const TranslateResponse &response);
	bool CheckHandleMessage(const TranslateResponse &response);
	bool CheckHandleRedirectBounceStatus(const TranslateResponse &response);

	bool DoContentTypeLookup(const ResourceAddress &address) noexcept;

	void OnAuthTranslateResponse(const TranslateResponse &response) noexcept;
	void OnAuthTranslateError(std::exception_ptr ep) noexcept;

	void OnHttpAuthTranslateResponse(const TranslateResponse &response) noexcept;
	void OnHttpAuthTranslateError(std::exception_ptr ep) noexcept;

	/**
	 * Handle #TRANSLATE_AUTH.
	 */
	void HandleAuth(const TranslateResponse &response);

	/**
	 * Handle #TranslationCommand::HTTP_AUTH.
	 */
	void HandleHttpAuth(const TranslateResponse &response) noexcept;

	bool EvaluateFileRequest(FileDescriptor fd, const struct statx &st,
				 struct file_request &file_request) noexcept;

	void DispatchFile(const char *path, UniqueFileDescriptor fd,
			  const struct statx &st,
			  const struct file_request &file_request) noexcept;

	bool DispatchCompressedFile(const char *path, FileDescriptor fd,
				    const struct statx &st,
				    const char *encoding) noexcept;

	bool CheckCompressedFile(const char *path, FileDescriptor fd,
				 const struct statx &st,
				 const char *encoding) noexcept;

	bool CheckAutoCompressedFile(const char *path, FileDescriptor fd,
				     const struct statx &st,
				     const char *encoding,
				     const char *suffix) noexcept;

	bool EmulateModAuthEasy(const FileAddress &address,
				UniqueFileDescriptor &fd,
				const struct statx &st) noexcept;

	bool MaybeEmulateModAuthEasy(const FileAddress &address,
				     UniqueFileDescriptor &fd,
				     const struct statx &st) noexcept;

	void HandleFileAddress(const FileAddress &address) noexcept;
	void HandleFileAddress(const FileAddress &address,
			       UniqueFileDescriptor fd,
			       const struct statx &st) noexcept;

	void HandleDelegateAddress(const DelegateAddress &address,
				   const char *path) noexcept;

	void HandleNfsAddress() noexcept;

	/**
	 * Return a copy of the original request URI for forwarding to the
	 * next server.  This omits the beng-proxy request "arguments"
	 * (unless the translation server declared the "transparent"
	 * mode).
	 */
	gcc_pure
	const char *ForwardURI() const noexcept;

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

	void HandleChainResponse(const TranslateResponse &response) noexcept;

	bool IsTransformationEnabled() const {
		return !translate.response->views->transformations.empty();
	}

	/**
	 * Returns true if the first transformation (if any) is the
	 * processor.
	 */
	bool IsProcessorFirst() const {
		return IsTransformationEnabled() &&
			translate.response->views->transformations.front().type
			== Transformation::Type::PROCESS;
	}

	bool IsProcessorEnabled() const;

	bool HasTransformations() const {
		return !translate.transformations.empty() ||
			translate.suffix_transformations.empty();
	}

	void CancelTransformations() {
		translate.transformations.clear();
		translate.suffix_transformations.clear();
	}

	const Transformation *PopTransformation() {
		if (!translate.suffix_transformations.empty()) {
			const auto &result = translate.suffix_transformations.front();
			translate.suffix_transformations.pop_front();
			return &result;
		} else if (!translate.transformations.empty()) {
			const auto &result = translate.transformations.front();
			translate.transformations.pop_front();
			return &result;
		} else
			return nullptr;
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

	StringMap ForwardRequestHeaders(const StringMap &src,
					bool exclude_host,
					bool with_body,
					bool forward_charset,
					bool forward_encoding,
					bool forward_range,
					const HeaderForwardSettings &settings,
					const char *host_and_port,
					const char *uri) noexcept;

	StringMap ForwardResponseHeaders(http_status_t status,
					 const StringMap &src,
					 const char *(*relocate)(const char *uri,
								 void *ctx) noexcept,
					 void *relocate_ctx,
					 const HeaderForwardSettings &settings) noexcept;

	void DispatchResponseDirect(http_status_t status, HttpHeaders &&headers,
				    UnusedIstreamPtr body);

	void DispatchResponse(http_status_t status, HttpHeaders &&headers,
			      UnusedIstreamPtr body);

	/**
	 * Dispatch an error generated by beng-proxy internally.  This
	 * may skip things like filters.
	 */
	void DispatchError(http_status_t status, HttpHeaders &&headers,
			   UnusedIstreamPtr body) noexcept;

	void DispatchError(http_status_t status, HttpHeaders &&headers,
			   std::nullptr_t) noexcept {
		DispatchError(status, std::move(headers), UnusedIstreamPtr());
	}

	void DispatchError(http_status_t status, const char *msg) noexcept;

	void DispatchError(http_status_t status, HttpHeaders &&headers,
			   const char *msg) noexcept;

	void DispatchRedirect(http_status_t status, const char *location,
			      const char *msg) noexcept;

	SharedPoolPtr<WidgetContext> MakeWidgetContext() noexcept;

private:
	UnusedIstreamPtr AutoDeflate(HttpHeaders &response_headers,
				     UnusedIstreamPtr response_body);

	SharedPoolPtr<WidgetContext> NewWidgetContext() const noexcept;

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

private:
	/**
	 * Asks the translation server for an error document, and submits it
	 * to InvokeResponse().  If there is no error document, or the
	 * error document resource fails, it resubmits the original response.
	 *
	 * @param error_document the payload of the #TRANSLATE_ERROR_DOCUMENT
	 * translate response packet
	 */
	void DisaptchErrdocResponse(http_status_t status,
				    ConstBuffer<void> error_document,
				    HttpHeaders &&headers,
				    UnusedIstreamPtr body) noexcept;

	/* FILE_DIRECTORY_INDEX handler */

	/**
	 * The #TranslateResponse contains #TRANSLATE_DIRECTORY_INDEX.
	 * Check if the file is a directory, and if it is,
	 * retranslate.
	 *
	 * @return true to continue handling the request, false on
	 * error or if retranslation has been triggered
	 */
	bool CheckDirectoryIndex(const TranslateResponse &response) noexcept;

	/* FILE_ENOTDIR handler */

	bool SubmitEnotdir(const TranslateResponse &response) noexcept;

	/**
	 * The #TranslateResponse contains #TRANSLATE_ENOTDIR.  Check this
	 * condition and retranslate.
	 *
	 * @return true to continue handling the request, false on error or if
	 * retranslation has been triggered
	 */
	bool CheckFileEnotdir(const TranslateResponse &response) noexcept;

	/**
	 * Append the ENOTDIR PATH_INFO to the resource address.
	 */
	void ApplyFileEnotdir() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		DiscardRequestBody();

		/* forward the abort to the http_server library */
		cancel_ptr.Cancel();

		Destroy();
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(TranslateResponse &response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class DelegateHandler */
	void OnDelegateSuccess(UniqueFileDescriptor fd) override;
	void OnDelegateError(std::exception_ptr ep) override;

#ifdef HAVE_URING
	/* virtual methods from class Uring::OpenStatHandler */
	void OnOpenStat(UniqueFileDescriptor fd,
			struct statx &st) noexcept override;
	void OnOpenStatError(std::exception_ptr e) noexcept override;
#endif

#ifdef HAVE_LIBNFS
	/* virtual methods from class NfsCacheHandler */
	void OnNfsCacheResponse(NfsCacheHandle &handle,
				const struct statx &st) noexcept override;
	void OnNfsCacheError(std::exception_ptr ep) noexcept override;
#endif

	/* virtual methods from class SuffixRegistryHandler */
	void OnSuffixRegistrySuccess(const char *content_type,
				     const IntrusiveForwardList<Transformation> &transformations) noexcept override;
	void OnSuffixRegistryError(std::exception_ptr ep) noexcept override;
};

/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "AnyClient.hxx"
#include "PendingRequest.hxx"
#include "ResponseHandler.hxx"
#include "Address.hxx"
#include "GlueClient.hxx"
#include "pool/pool.hxx"
#include "nghttp2/Glue.hxx"
#include "ssl/SslSocketFilterFactory.hxx"
#include "fs/Balancer.hxx"
#include "fs/FilteredSocket.hxx"
#include "fs/Handler.hxx"
#include "fs/Key.hxx"
#include "fs/Stock.hxx"
#include "event/DeferEvent.hxx"
#include "net/HostParser.hxx"
#include "util/Cancellable.hxx"
#include "util/IntrusiveList.hxx"
#include "util/StringBuilder.hxx"
#include "stopwatch.hxx"

#ifdef HAVE_NGHTTP2

/**
 * Basic data for a HTTP/2 probe request waiting to be submitted to a
 * server.  This is a separate struct because it needs to be moved to
 * the stack under certain conditions.
 */
struct AnyHttpClient::Request {
	struct pool &pool;
	HttpResponseHandler &handler;

	const StopwatchPtr parent_stopwatch;

	const sticky_hash_t sticky_hash;

	SocketFilterFactory &filter_factory;

	const HttpAddress &address;
	PendingHttpRequest pending_request;

	CancellablePointer &caller_cancel_ptr;

	Request(struct pool &_pool,
		HttpResponseHandler &_handler,
		const StopwatchPtr &_parent_stopwatch,
		sticky_hash_t _sticky_hash,
		SocketFilterFactory &_filter_factory,
		http_method_t _method,
		const HttpAddress &_address,
		StringMap &&_headers, UnusedIstreamPtr _body,
		CancellablePointer &_caller_cancel_ptr) noexcept
		:pool(_pool), handler(_handler),
		 parent_stopwatch(_parent_stopwatch),
		 sticky_hash(_sticky_hash),
		 filter_factory(_filter_factory),
		 address(_address),
		 pending_request(pool, _method, address.path,
				 std::move(_headers), std::move(_body)),
		 caller_cancel_ptr(_caller_cancel_ptr)
	{
	}

	void SendHTTP1(EventLoop &event_loop,
		       FilteredSocketBalancer &fs_balancer) noexcept {
		http_request(pool, event_loop, fs_balancer,
			     parent_stopwatch,
			     sticky_hash,
			     &filter_factory,
			     pending_request.method, address,
			     std::move(pending_request.headers),
			     std::move(pending_request.body),
			     handler, caller_cancel_ptr);
	}

	void SendHTTP2(EventLoop &event_loop,
		       NgHttp2::Stock &nghttp2_stock,
		       NgHttp2::AlpnHandler *alpn_handler,
		       CancellablePointer &cancel_ptr) noexcept {
		NgHttp2::SendRequest(pool, event_loop, nghttp2_stock,
				     parent_stopwatch,
				     &filter_factory,
				     pending_request.method, address,
				     std::move(pending_request.headers),
				     std::move(pending_request.body),
				     alpn_handler,
				     handler, cancel_ptr);
	}
};

/**
 * A request to probe the protocol, or one waiting for the probe on
 * this server.
 */
struct AnyHttpClient::Waiting final
	: AutoUnlinkIntrusiveListHook, Cancellable
{
	Probe &parent;

	Request request;

	CancellablePointer cancel_ptr;

	Waiting(Probe &_parent, struct pool &_pool,
		HttpResponseHandler &_handler,
		const StopwatchPtr &_parent_stopwatch,
		sticky_hash_t _sticky_hash,
		SocketFilterFactory &_filter_factory,
		http_method_t _method,
		const HttpAddress &_address,
		StringMap &&_headers, UnusedIstreamPtr _body,
		CancellablePointer &_caller_cancel_ptr) noexcept
		:parent(_parent),
		 request(_pool, _handler, _parent_stopwatch, _sticky_hash,
			 _filter_factory, _method, _address,
			 std::move(_headers), std::move(_body),
			 _caller_cancel_ptr)
	{
		_caller_cancel_ptr = *this;
	}

	~Waiting() noexcept {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	void Destroy() noexcept {
		this->~Waiting();
	}

	struct Disposer {
		void operator()(Waiting *w) noexcept {
			w->Destroy();
		}
	};

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		Destroy();
	}
};

/**
 * This class manages the probe request to one server and remembers
 * the result.  Additionally, it manages a queue of additional
 * requests which have arrived while the probe was running.
 */
class AnyHttpClient::Probe final : NgHttp2::AlpnHandler {
	AnyHttpClient &parent;

	const std::string key;

	IntrusiveList<Waiting> waiting;

	DeferEvent defer_again;

	enum class State {
		UNKNOWN,
		PENDING,
		HTTP2,
		HTTP1,
	} state = State::UNKNOWN;

public:
	explicit Probe(AnyHttpClient &_parent, const char *_key) noexcept
		:parent(_parent), key(_key),
		 defer_again(parent.GetEventLoop(), BIND_THIS_METHOD(OnAgain))
	{
	}

	auto &GetEventLoop() const noexcept {
		return defer_again.GetEventLoop();
	}

	void SendRequest(struct pool &pool,
			 const StopwatchPtr &parent_stopwatch,
			 sticky_hash_t sticky_hash,
			 SocketFilterFactory &filter_factory,
			 http_method_t method,
			 const HttpAddress &address,
			 StringMap &&headers, UnusedIstreamPtr body,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept;

private:
	void OnAgain() noexcept;

	void SendHTTP1(Request &r) noexcept {
		r.SendHTTP1(GetEventLoop(), parent.fs_balancer);
	}

	void SendHTTP2(Request &r, CancellablePointer &cancel_ptr) noexcept {
		r.SendHTTP2(GetEventLoop(), parent.nghttp2_stock,
			    this,  cancel_ptr);
	}

	/* virtual methods from class NgHttp2::AlpnHandler */
	void OnAlpnError() noexcept override;
	void OnAlpnNoMismatch() noexcept override;
	void OnAlpnMismatch(PendingHttpRequest &&pending_request,
			    SocketAddress address,
			    std::unique_ptr<FilteredSocket> &&socket) noexcept override;
};

inline void
AnyHttpClient::Probe::SendRequest(struct pool &pool,
				  const StopwatchPtr &parent_stopwatch,
				  sticky_hash_t sticky_hash,
				  SocketFilterFactory &filter_factory,
				  http_method_t method,
				  const HttpAddress &address,
				  StringMap &&headers, UnusedIstreamPtr body,
				  HttpResponseHandler &handler,
				  CancellablePointer &cancel_ptr) noexcept
{
	switch (state) {
	case State::UNKNOWN:
		break;

	case State::PENDING:
		assert(!waiting.empty());
		break;

	case State::HTTP2:
		NgHttp2::SendRequest(pool, GetEventLoop(),
				     parent.nghttp2_stock,
				     parent_stopwatch,
				     &filter_factory,
				     method, address,
				     std::move(headers),
				     std::move(body),
				     nullptr,
				     handler, cancel_ptr);
		return;

	case State::HTTP1:
		http_request(pool, GetEventLoop(), parent.fs_balancer,
			     parent_stopwatch,
			     sticky_hash,
			     &filter_factory,
			     method, address,
			     std::move(headers),
			     std::move(body),
			     handler, cancel_ptr);
		return;
	}

	auto *w = NewFromPool<Waiting>(pool, *this, pool, handler,
				       parent_stopwatch,
				       sticky_hash, filter_factory,
				       method, address,
				       std::move(headers), std::move(body),
				       cancel_ptr);
	waiting.push_back(*w);

	if (state == State::UNKNOWN)
		defer_again.Schedule();
}

void
AnyHttpClient::Probe::OnAgain() noexcept
{
	if (waiting.empty())
		return;

	auto &w = waiting.front();

	switch (state) {
	case State::PENDING:
		break;

	case State::UNKNOWN:
		/* try HTTP/2, but let the AlpnHandler deal with ALPN
		   failures */
		state = State::PENDING;

		w.request.SendHTTP2(GetEventLoop(), parent.nghttp2_stock,
				    this, w.cancel_ptr);
		break;

	case State::HTTP2:
		// HTTP/2 is supported
		if (auto r = std::move(w.request); true) {
			waiting.erase_and_dispose(waiting.iterator_to(w),
						  Waiting::Disposer{});
			r.SendHTTP2(GetEventLoop(), parent.nghttp2_stock,
				    nullptr, r.caller_cancel_ptr);
		}

		break;

	case State::HTTP1:
		// only HTTP/1.1 is supported
		if (auto r = std::move(w.request); true) {
			waiting.erase_and_dispose(waiting.iterator_to(w),
						  Waiting::Disposer{});
			r.SendHTTP1(GetEventLoop(), parent.fs_balancer);
		}

		break;
	}
}

void
AnyHttpClient::Probe::OnAlpnError() noexcept
{
	assert(state == State::PENDING);
	assert(!waiting.empty());

	state = State::UNKNOWN;

	/* this request failed - remove it (and don't cancel it) */
	auto &w = waiting.front();
	w.cancel_ptr = {};
	waiting.pop_front_and_dispose(Waiting::Disposer{});

	defer_again.Schedule();
}

void
AnyHttpClient::Probe::OnAlpnNoMismatch() noexcept
{
	assert(state == State::PENDING);
	assert(!waiting.empty());

	/* HTTP/2 connection successful - remove this request, the
	   rest will be handled without us */
	auto &w = waiting.front();
	w.cancel_ptr = {};
	waiting.pop_front_and_dispose(Waiting::Disposer{});

	state = State::HTTP2;

	defer_again.Schedule();
}

void
AnyHttpClient::Probe::OnAlpnMismatch(PendingHttpRequest &&pending_request,
				     SocketAddress address,
				     std::unique_ptr<FilteredSocket> &&socket) noexcept
{
	assert(state == State::PENDING);
	assert(!waiting.empty());

	state = State::HTTP1;

	/* add this HTTP/1.1 socket to the FilteredSocketStock so it
	   will be used by the deferred SendHTTP1() call */
	parent.fs_balancer.GetStock().Add(key.c_str(), address,
					  std::move(socket));

	auto &w = waiting.front();
	w.cancel_ptr = {};
	w.request.pending_request = std::move(pending_request);

	/* schedule a call to SendHTTP1() */
	defer_again.Schedule();
}

#endif // HAVE_NGHTTP2

AnyHttpClient::AnyHttpClient(FilteredSocketBalancer &_fs_balancer,
#ifdef HAVE_NGHTTP2
			     NgHttp2::Stock &_nghttp2_stock,
#endif
			     SslClientFactory *_ssl_client_factory) noexcept
	:fs_balancer(_fs_balancer),
#ifdef HAVE_NGHTTP2
	 nghttp2_stock(_nghttp2_stock),
#endif
	 ssl_client_factory(_ssl_client_factory)
{
}

AnyHttpClient::~AnyHttpClient() noexcept = default;

inline EventLoop &
AnyHttpClient::GetEventLoop() const noexcept
{
	return fs_balancer.GetEventLoop();
}

[[gnu::pure]]
static const char *
GetHostWithoutPort(struct pool &pool, const HttpAddress &address) noexcept
{
	const char *host_and_port = address.host_and_port;
	if (host_and_port == nullptr)
		return nullptr;

	auto e = ExtractHost(host_and_port);
	if (e.host.IsNull())
		return nullptr;

	return p_strdup(pool, e.host);
}

#ifdef HAVE_NGHTTP2

inline void
AnyHttpClient::ProbeHTTP2(struct pool &pool,
			  const StopwatchPtr &parent_stopwatch,
			  sticky_hash_t sticky_hash,
			  SocketFilterFactory &filter_factory,
			  http_method_t method,
			  const HttpAddress &address,
			  StringMap &&headers, UnusedIstreamPtr body,
			  HttpResponseHandler &handler,
			  CancellablePointer &cancel_ptr) noexcept
{
	char key_buffer[1024];

	try {
		StringBuilder b(key_buffer);

		const char *const name = nullptr;
		const SocketAddress bind_address = nullptr;
		MakeFilteredSocketStockKey(b, name,
					   bind_address,
					   *address.addresses.begin(), // TODO
					   &filter_factory);
	} catch (StringBuilder::Overflow) {
		/* shouldn't happen */
		handler.InvokeError(std::current_exception());
		return;
	}

	auto &probe = probes.try_emplace(key_buffer, *this, key_buffer)
		.first->second;
	probe.SendRequest(pool, parent_stopwatch, sticky_hash,
			  filter_factory, method, address,
			  std::move(headers), std::move(body),
			  handler, cancel_ptr);
	return;
}

#endif // HAVE_NGHTTP2

void
AnyHttpClient::SendRequest(struct pool &pool,
			   const StopwatchPtr &parent_stopwatch,
			   sticky_hash_t sticky_hash,
			   http_method_t method,
			   const HttpAddress &address,
			   StringMap &&headers, UnusedIstreamPtr body,
			   HttpResponseHandler &handler,
			   CancellablePointer &cancel_ptr)
{
	auto &event_loop = GetEventLoop();

	SocketFilterFactory *filter_factory = nullptr;

	if (address.ssl) {
		if (ssl_client_factory == nullptr)
			throw std::runtime_error("SSL support is disabled");

		auto alpn = address.http2
			? SslClientAlpn::HTTP_2
			: SslClientAlpn::HTTP_ANY;

		filter_factory = NewFromPool<SslSocketFilterFactory>(pool,
								     event_loop,
								     *ssl_client_factory,
								     GetHostWithoutPort(pool, address),
								     address.certificate,
								     alpn);

#ifdef HAVE_NGHTTP2
		if (!address.http2) {
			ProbeHTTP2(pool, parent_stopwatch, sticky_hash,
				   *filter_factory,
				   method, address,
				   std::move(headers), std::move(body),
				   handler, cancel_ptr);
			return;
		}
#endif
	}

#ifdef HAVE_NGHTTP2
	if (address.http2)
		NgHttp2::SendRequest(pool, event_loop, nghttp2_stock,
				     parent_stopwatch,
				     filter_factory,
				     method, address,
				     std::move(headers),
				     std::move(body),
				     nullptr,
				     handler, cancel_ptr);
	else
#endif
		http_request(pool, event_loop, fs_balancer,
			     parent_stopwatch,
			     sticky_hash,
			     filter_factory,
			     method, address,
			     std::move(headers),
			     std::move(body),
			     handler, cancel_ptr);
}

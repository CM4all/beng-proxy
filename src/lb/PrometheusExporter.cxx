// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PrometheusExporter.hxx"
#include "PrometheusExporterConfig.hxx"
#include "Instance.hxx"
#include "Listener.hxx"
#include "Config.hxx"
#include "prometheus/Stats.hxx"
#include "prometheus/HttpStats.hxx"
#include "pool/LeakDetector.hxx"
#include "thread/Pool.hxx"
#include "net/control/Protocol.hxx"
#include "http/Address.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Method.hxx"
#include "http/PHeaderUtil.cxx"
#include "http/ResponseHandler.hxx"
#include "http/GlueClient.hxx"
#include "util/Cancellable.hxx"
#include "util/MimeType.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/GzipIstream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "istream/CatchIstream.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "net/TimeoutError.hxx"
#include "memory/istream_gb.hxx"
#include "memory/GrowingBuffer.hxx"
#include "event/PrometheusStats.hxx"
#include "stopwatch.hxx"

using std::string_view_literals::operator""sv;

class LbPrometheusExporter::AppendRequest final
	: public HttpResponseHandler, Cancellable, PoolLeakDetector
{
	DelayedIstreamControl &control;

	const SocketAddress socket_address;

	HttpAddress address{false, "dummy:80", "/metrics"};

	CoarseTimerEvent timeout_event;

	CancellablePointer cancel_ptr;

public:
	AppendRequest(struct pool &_pool,
		      EventLoop &event_loop,
		      SocketAddress _address,
		      DelayedIstreamControl &_control) noexcept
		:PoolLeakDetector(_pool),
		 control(_control), socket_address(_address),
		 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout))
	{
		control.cancel_ptr = *this;

		address.addresses = AddressList{
			ShallowCopy{},
			StickyMode::NONE,
			std::span{&socket_address, 1},
		};
	}

	void Start(struct pool &pool, LbInstance &instance) noexcept;

	void Destroy() noexcept {
		this->~AppendRequest();
	}

	void DestroyError(std::exception_ptr error) noexcept {
		auto &_control = control;
		Destroy();
		_control.SetError(std::move(error));
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;

	void OnHttpError(std::exception_ptr error) noexcept override {
		DestroyError(std::move(error));
	}

private:
	void OnTimeout() noexcept {
		/* this request has been taking too long - cancel it
		   and report timeout */
		cancel_ptr.Cancel();
		DestroyError(std::make_exception_ptr(TimeoutError{}));
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}
};

inline void
LbPrometheusExporter::AppendRequest::Start(struct pool &pool,
					   LbInstance &_instance) noexcept
{
	timeout_event.Schedule(std::chrono::seconds{10});

	http_request(pool, _instance.event_loop,
		     *_instance.fs_balancer, {}, {},
		     nullptr,
		     HttpMethod::GET, address, {}, nullptr,
		     *this, cancel_ptr);
}

void
LbPrometheusExporter::AppendRequest::OnHttpResponse(HttpStatus status,
						    StringMap &&headers,
						    UnusedIstreamPtr body) noexcept
try {
	if (!http_status_is_success(status))
		throw std::runtime_error("HTTP request not sucessful");

	const char *content_type = headers.Get(content_type_header);
	if (content_type == nullptr ||
	    GetMimeTypeBase(content_type) != "text/plain")
		throw std::runtime_error("Not text/plain");

	auto &_control = control;
	Destroy();
	_control.Set(std::move(body));
} catch (...) {
	DestroyError(std::current_exception());
}

static std::exception_ptr
CatchCallback(std::exception_ptr) noexcept
{
	// TODO log?
	return {};
}

static void
WriteStats(GrowingBuffer &buffer, const LbInstance &instance) noexcept
{
	constexpr auto process = "lb"sv;

	buffer.Write(ToPrometheusString(instance.event_loop.GetStats(), process));
	Prometheus::Write(buffer, process, instance.GetStats());

	for (const auto &listener : instance.listeners)
		if (const auto *stats = listener.GetHttpStats())
			Prometheus::Write(buffer, process,
					  listener.GetConfig().name,
					  *stats);
}

void
LbPrometheusExporter::HandleHttpRequest(IncomingHttpRequest &request,
					const StopwatchPtr &,
					CancellablePointer &) noexcept
{
	auto &pool = request.pool;

	GrowingBuffer buffer;

	if (instance != nullptr)
		WriteStats(buffer, *instance);

	HttpHeaders headers;
	headers.Write("content-type", "text/plain;version=0.0.4");

	auto body = NewConcatIstream(pool,
				     istream_gb_new(pool, std::move(buffer)));

	for (const auto &i : config.load_from_local) {
		// TODO check instance!=nullptr
		auto delayed = istream_delayed_new(pool, instance->event_loop);
		UnusedHoldIstreamPtr hold(pool, std::move(delayed.first));

		auto *ar = NewFromPool<AppendRequest>(pool, pool, instance->event_loop, i, delayed.second);
		ar->Start(pool, *instance);

		AppendConcatIstream(body,
				    NewCatchIstream(pool,
						    std::move(hold),
						    BIND_FUNCTION(CatchCallback)));
	}

	if (instance != nullptr && http_client_accepts_encoding(request.headers, "gzip")) {
		headers.Write("content-encoding", "gzip");
		body = NewGzipIstream(pool,
				      thread_pool_get_queue(instance->event_loop),
				      std::move(body));
	}

	request.SendResponse(HttpStatus::OK, std::move(headers),
			     std::move(body));
}

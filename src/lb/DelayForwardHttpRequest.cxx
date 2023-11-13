// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "DelayForwardHttpRequest.hxx"
#include "ForwardHttpRequest.hxx"
#include "HttpConnection.hxx"
#include "Instance.hxx"
#include "Listener.hxx"
#include "http/IncomingRequest.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "pool/LeakDetector.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "util/Cancellable.hxx"

class LbDelayRequest final
	: PoolLeakDetector, Cancellable
{
	LbHttpConnection &connection;
	IncomingHttpRequest &request;

	/**
	 * This object temporarily holds the request body
	 */
	UnusedHoldIstreamPtr request_body;

	LbCluster &cluster;
	CancellablePointer &cancel_ptr;

	CoarseTimerEvent timer;

public:
	LbDelayRequest(LbHttpConnection &_connection,
		       IncomingHttpRequest &_request,
		       LbCluster &_cluster,
		       CancellablePointer &_cancel_ptr) noexcept
		:PoolLeakDetector(_request.pool),
		 connection(_connection),
		 request(_request),
		 request_body(request.pool, std::move(request.body)),
		 cluster(_cluster),
		 cancel_ptr(_cancel_ptr),
		 timer(connection.instance.event_loop,
		       BIND_THIS_METHOD(OnTimer))
	{
		_cancel_ptr = *this;
	}

	EventLoop &GetEventLoop() const noexcept {
		return timer.GetEventLoop();
	}

	void Start(Event::Duration delay) noexcept {
		timer.Schedule(delay);
	}

private:
	void Destroy() noexcept {
		DeleteFromPool(request.pool, this);
	}

	void OnTimer() noexcept {
		request.body = std::move(request_body);

		auto &_connection = connection;
		auto &_request = request;
		auto &_cluster = cluster;
		auto &_cancel_ptr = cancel_ptr;

		Destroy();
		ForwardHttpRequest(_connection, _request, _cluster, _cancel_ptr);
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		/* do not bother to log requests that have been
		   delayed artificially already; this is probably a
		   DoS and logging it would only consume more of our
		   resources */
		request.logger = nullptr;

		connection.RecordAbuse();

		Destroy();
	}
};

void
DelayForwardHttpRequest(LbHttpConnection &connection,
			IncomingHttpRequest &request,
			LbCluster &cluster,
			Event::Duration delay,
			CancellablePointer &cancel_ptr) noexcept
{
	++connection.instance.http_stats.n_delayed;
	++connection.listener.GetHttpStats().n_delayed;

	const auto request2 =
		NewFromPool<LbDelayRequest>(request.pool,
					    connection,  request, cluster,
					    cancel_ptr);
	request2->Start(delay);
}

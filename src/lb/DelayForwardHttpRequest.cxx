/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "DelayForwardHttpRequest.hxx"
#include "ForwardHttpRequest.hxx"
#include "HttpConnection.hxx"
#include "Instance.hxx"
#include "http/IncomingRequest.hxx"
#include "pool/LeakDetector.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "util/Cancellable.hxx"

class LbDelayRequest final
	: PoolLeakDetector, Cancellable
{
	LbHttpConnection &connection;
	IncomingHttpRequest &request;
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

	void Start() noexcept {
		timer.Schedule(std::chrono::seconds{10});
	}

private:
	void Destroy() noexcept {
		DeleteFromPool(request.pool, this);
	}

	void OnTimer() noexcept {
		auto &_connection = connection;
		auto &_request = request;
		auto &_cluster = cluster;
		auto &_cancel_ptr = cancel_ptr;

		Destroy();
		ForwardHttpRequest(_connection, _request, _cluster, _cancel_ptr);
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		Destroy();
	}
};

void
DelayForwardHttpRequest(LbHttpConnection &connection,
			IncomingHttpRequest &request,
			LbCluster &cluster,
			CancellablePointer &cancel_ptr) noexcept
{
	const auto request2 =
		NewFromPool<LbDelayRequest>(request.pool,
					    connection,  request, cluster,
					    cancel_ptr);
	request2->Start();
}

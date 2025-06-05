// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ExternalSession.hxx"
#include "session/Session.hxx"
#include "Instance.hxx"
#include "http/Address.hxx"
#include "http/GlueClient.hxx"
#include "http/ResponseHandler.hxx"
#include "http/Method.hxx"
#include "istream/UnusedPtr.hxx"
#include "AllocatorPtr.hxx"
#include "pool/Holder.hxx"
#include "io/Logger.hxx"
#include "util/Background.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"

class ExternalSessionRefresh final
	: PoolHolder, public BackgroundJob, HttpResponseHandler {

	const HttpAddress address;

public:
	ExternalSessionRefresh(PoolPtr &&_pool,
			       const HttpAddress &_address)
		:PoolHolder(std::move(_pool)),
		 address(GetPool(), _address) {}

	void SendRequest(BpInstance &instance, const SessionId session_id) {
		http_request(pool, instance.event_loop, *instance.fs_balancer,
			     nullptr,
			     session_id.GetClusterHash(),
			     nullptr,
			     HttpMethod::GET, address,
			     {}, nullptr,
			     *this, cancel_ptr);
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status,
			    StringMap &&,
			    UnusedIstreamPtr body) noexcept override {
		body.Clear();

		if (!http_status_is_success(status))
			LogConcat(3, "ExternalSessionManager", "Status ", int(status),
				  " from manager '", address.path, "'");

		unlink();
	}

	void OnHttpError(std::exception_ptr ep) noexcept override {
		LogConcat(2, "ExternalSessionManager", "Failed to refresh external session: ", ep);

		unlink();
	}
};

void
RefreshExternalSession(BpInstance &instance, Session &session)
{
	if (session.external_manager == nullptr ||
	    session.external_keepalive <= std::chrono::seconds::zero())
		/* feature is not enabled */
		return;

	const auto now = instance.event_loop.SteadyNow();
	if (now < session.next_external_keepalive)
		/* not yet */
		return;

	LogConcat(5, "ExternalSessionManager", "refresh '",
		  session.external_manager->path, "'");

	session.next_external_keepalive = now + session.external_keepalive;

	auto pool = pool_new_linear(instance.root_pool, "external_session_refresh",
				    4096);

	auto *refresh = NewFromPool<ExternalSessionRefresh>(std::move(pool),
							    *session.external_manager);
	instance.background_manager.Add(*refresh);

	refresh->SendRequest(instance, session.id);
}

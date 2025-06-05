// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Request.hxx"
#include "RLogger.hxx"
#include "Connection.hxx"
#include "Listener.hxx"
#include "access_log/Glue.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "net/Parser.hxx"
#include "AllocatorPtr.hxx"

SocketAddress
Request::GetRemoteAdress() const noexcept
{
	const auto *const _config = connection.listener.GetXForwardedForConfig();
	if (_config == nullptr)
		return request.remote_address;

	const auto &config = *_config;

	const char *x_forwarded_for = request.headers.Get(x_forwarded_for_header);
	if (x_forwarded_for == nullptr)
		return request.remote_address;

	if ((request.remote_host != nullptr && config.IsTrustedHost(request.remote_host)) ||
	    config.IsTrustedAddress(request.remote_address)) {
		const auto r = config.GetRealRemoteHost(x_forwarded_for);
		if (!r.empty()) {
			try {
				const auto address = ParseSocketAddress(std::string{r}.c_str(), 0, false);
				if (!address.IsNull()) {
					const AllocatorPtr alloc{pool};
					return alloc.Dup(address);
				}
			} catch (...) {
			}
		}
	}

	return request.remote_address;
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Context.hxx"
#include "bp/ForwardHeaders.hxx"
#include "bp/session/Lease.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

StringMap
WidgetContext::ForwardRequestHeaders(AllocatorPtr alloc,
				     bool exclude_host,
				     bool with_body,
				     bool forward_charset,
				     bool forward_encoding,
				     bool forward_range,
				     const HeaderForwardSettings &settings,
				     const char *host_and_port,
				     const char *_uri) noexcept
{
	return forward_request_headers(alloc, *request_headers,
				       local_host, remote_host,
				       peer_subject, peer_issuer_subject,
				       exclude_host,
				       with_body,
				       forward_charset,
				       forward_encoding,
				       forward_range,
				       settings,
				       session_cookie,
				       GetRealmSession().get(),
				       user,
				       nullptr,
				       host_and_port, _uri);
}

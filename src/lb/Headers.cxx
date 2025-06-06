// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Headers.hxx"
#include "strmap.hxx"
#include "AllocatorPtr.hxx"
#include "http/CommonHeaders.hxx"

static void
forward_via(AllocatorPtr alloc, StringMap &headers,
	    const char *local_host) noexcept
{
	const char *p = headers.RemoveAll(via_header);
	if (p == nullptr) {
		if (local_host != nullptr)
			headers.Add(alloc, via_header, alloc.Concat("1.1 ", local_host));
	} else {
		if (local_host == nullptr)
			headers.Add(alloc, via_header, p);
		else
			headers.Add(alloc, via_header, alloc.Concat(p, ", 1.1 ", local_host));
	}
}

static void
forward_xff(AllocatorPtr alloc, StringMap &headers,
	    const char *remote_host) noexcept
{
	const char *p = headers.RemoveAll(x_forwarded_for_header);
	if (p == nullptr) {
		if (remote_host != nullptr)
			headers.Add(alloc, x_forwarded_for_header, remote_host);
	} else {
		if (remote_host == nullptr)
			headers.Add(alloc, x_forwarded_for_header, p);
		else
			headers.Add(alloc, x_forwarded_for_header,
				    alloc.Concat(p, ", ", remote_host));
	}
}

static void
forward_identity(AllocatorPtr alloc, StringMap &headers,
		 const char *local_host, const char *remote_host) noexcept
{
	forward_via(alloc, headers, local_host);
	forward_xff(alloc, headers, remote_host);
}

void
lb_forward_request_headers(AllocatorPtr alloc, StringMap &headers,
			   const char *local_host, const char *remote_host,
			   bool https,
			   const char *peer_subject,
			   const char *peer_issuer_subject,
			   bool mangle_via) noexcept
{
	headers.SecureSet(alloc, x_cm4all_https_header, https ? "on" : nullptr);

	headers.SecureSet(alloc, x_cm4all_beng_peer_subject_header, peer_subject);
	headers.SecureSet(alloc, x_cm4all_beng_peer_issuer_subject_header,
			  peer_issuer_subject);

	if (mangle_via)
		forward_identity(alloc, headers, local_host, remote_host);
}

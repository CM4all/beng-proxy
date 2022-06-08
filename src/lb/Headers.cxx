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

#include "Headers.hxx"
#include "strmap.hxx"
#include "AllocatorPtr.hxx"

static void
forward_via(AllocatorPtr alloc, StringMap &headers,
	    const char *local_host) noexcept
{
	const char *p = headers.Remove("via");
	if (p == nullptr) {
		if (local_host != nullptr)
			headers.Add(alloc, "via", alloc.Concat("1.1 ", local_host));
	} else {
		if (local_host == nullptr)
			headers.Add(alloc, "via", p);
		else
			headers.Add(alloc, "via", alloc.Concat(p, ", 1.1 ", local_host));
	}
}

static void
forward_xff(AllocatorPtr alloc, StringMap &headers,
	    const char *remote_host) noexcept
{
	const char *p = headers.Remove("x-forwarded-for");
	if (p == nullptr) {
		if (remote_host != nullptr)
			headers.Add(alloc, "x-forwarded-for", remote_host);
	} else {
		if (remote_host == nullptr)
			headers.Add(alloc, "x-forwarded-for", p);
		else
			headers.Add(alloc, "x-forwarded-for",
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
	headers.SecureSet(alloc, "x-cm4all-https", https ? "on" : nullptr);

	headers.SecureSet(alloc, "x-cm4all-beng-peer-subject", peer_subject);
	headers.SecureSet(alloc, "x-cm4all-beng-peer-issuer-subject",
			  peer_issuer_subject);

	if (mangle_via)
		forward_identity(alloc, headers, local_host, remote_host);
}

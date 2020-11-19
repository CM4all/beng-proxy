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

#include "Context.hxx"
#include "bp/ForwardHeaders.hxx"
#include "bp/session/Session.hxx"
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
				       host_and_port, _uri);
}

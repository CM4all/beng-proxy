// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class AllocatorPtr;
class StringMap;

/**
 * Which headers should be forwarded to/from remote HTTP servers?
 */
void
lb_forward_request_headers(AllocatorPtr alloc, StringMap &headers,
			   const char *local_host, const char *remote_host,
			   bool https,
			   const char *peer_subject,
			   const char *peer_issuer_subject,
			   bool mangle_via) noexcept;

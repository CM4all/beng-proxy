// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <stdint.h>

struct pool;
class UnusedIstreamPtr;

/**
 * Convert a stream into a stream of FCGI_STDIN packets.
 *
 * @param request_id the FastCGI request id in network byte order
 */
UnusedIstreamPtr
istream_fcgi_new(struct pool &pool, UnusedIstreamPtr input,
		 uint16_t request_id) noexcept;

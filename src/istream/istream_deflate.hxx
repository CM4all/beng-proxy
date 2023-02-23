// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class EventLoop;

/**
 * @param gzip use the gzip format instead of the zlib format?
 */
UnusedIstreamPtr
istream_deflate_new(struct pool &pool, UnusedIstreamPtr input,
		    EventLoop &event_loop, bool gzip=false) noexcept;

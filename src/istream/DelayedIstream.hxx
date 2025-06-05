// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/Cancellable.hxx"

#include <exception>

struct pool;
class UnusedIstreamPtr;
class EventLoop;

class DelayedIstreamControl {
protected:
	DelayedIstreamControl() = default;
	~DelayedIstreamControl() = default;

	DelayedIstreamControl(const DelayedIstreamControl &) = delete;
	DelayedIstreamControl &operator=(const DelayedIstreamControl &) = delete;

public:
	CancellablePointer cancel_ptr;

	void Set(UnusedIstreamPtr input) noexcept;
	void SetEof() noexcept;

	/**
	 * Injects a failure, to be called instead of Set().
	 */
	void SetError(std::exception_ptr e) noexcept;
};

/**
 * An istream facade which waits for its inner istream to appear.
 */
std::pair<UnusedIstreamPtr, DelayedIstreamControl &>
istream_delayed_new(struct pool &pool, EventLoop &event_loop) noexcept;

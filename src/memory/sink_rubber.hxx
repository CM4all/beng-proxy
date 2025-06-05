// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstddef>
#include <exception>

struct pool;
class UnusedIstreamPtr;
class Rubber;
class RubberAllocation;
class RubberSink;
class CancellablePointer;

class RubberSinkHandler {
public:
	virtual void RubberDone(RubberAllocation &&a, size_t size) noexcept = 0;
	virtual void RubberOutOfMemory() noexcept = 0;
	virtual void RubberTooLarge() noexcept = 0;
	virtual void RubberError(std::exception_ptr ep) noexcept = 0;
};

/**
 * An istream sink that copies data into a rubber allocation.
 */
RubberSink *
sink_rubber_new(struct pool &pool, UnusedIstreamPtr input,
		Rubber &rubber, size_t max_size,
		RubberSinkHandler &handler,
		CancellablePointer &cancel_ptr) noexcept;

void
sink_rubber_read(RubberSink &sink) noexcept;

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "memory/SliceFifoBuffer.hxx"
#include "memory/fb_pool.hxx"

/**
 * A frontend for #SliceFifoBuffer which allows to replace it with a
 * simple heap-allocated buffer when some client code gets copied to
 * another project.
 */
class DefaultFifoBuffer : public SliceFifoBuffer {
public:
	void Allocate() noexcept {
		SliceFifoBuffer::Allocate(fb_pool_get());
	}

	void AllocateIfNull() noexcept {
		SliceFifoBuffer::AllocateIfNull(fb_pool_get());
	}

	void CycleIfEmpty() noexcept {
		SliceFifoBuffer::CycleIfEmpty(fb_pool_get());
	}
};

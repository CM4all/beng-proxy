/*
 * Copyright 2007-2018 Content Management AG
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

#pragma once

#include <cassert>
#include <cstddef>
#include <utility>

class SliceArea;

class SliceAllocation {
public:
	SliceArea *area;

	void *data = nullptr;
	std::size_t size;

	SliceAllocation() = default;

	SliceAllocation(SliceArea &_area, void *_data, std::size_t _size) noexcept
		:area(&_area), data(_data), size(_size) {}

	SliceAllocation(SliceAllocation &&src) noexcept
		:area(src.area),
		 data(std::exchange(src.data, nullptr)), size(src.size) {}

	~SliceAllocation() noexcept {
		if (IsDefined())
			Free();
	}

	SliceAllocation &operator=(SliceAllocation &&src) noexcept {
		using std::swap;
		swap(area, src.area);
		swap(data, src.data);
		swap(size, src.size);
		return *this;
	}

	friend void swap(SliceAllocation &a, SliceAllocation &b) noexcept {
		using std::swap;
		swap(a.area, b.area);
		swap(a.data, b.data);
		swap(a.size, b.size);
	}

	bool IsDefined() const noexcept {
		return data != nullptr;
	}

	void *Steal() noexcept {
		assert(IsDefined());

		return std::exchange(data, nullptr);
	}

	void Free() noexcept;
};

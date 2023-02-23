// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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

	/**
	 * This constructor is used if HaveMemoryChecker(); in that
	 * case, memory was allocated with malloc().
	 */
	explicit SliceAllocation(void *_data, std::size_t _size) noexcept
		:data(_data), size(_size) {}

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

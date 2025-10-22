// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstddef> // for std::nullptr_t
#include <utility>

#include <assert.h>
#include <sys/types.h>

struct IstreamLength;
class Istream;

/**
 * This class holds a pointer to an unused #Istream and auto-closes
 * it.  It can be moved to other instances, until it is finally
 * "stolen" using Steal() to actually use it.
 */
class UnusedIstreamPtr {
	Istream *stream = nullptr;

public:
	UnusedIstreamPtr() = default;
	UnusedIstreamPtr(std::nullptr_t) noexcept {}

	explicit UnusedIstreamPtr(Istream *_stream) noexcept
		:stream(_stream) {}

	UnusedIstreamPtr(UnusedIstreamPtr &&src) noexcept
		:stream(std::exchange(src.stream, nullptr)) {}

	~UnusedIstreamPtr() noexcept {
		if (stream != nullptr)
			Close(*stream);
	}

	UnusedIstreamPtr &operator=(UnusedIstreamPtr &&src) noexcept {
		using std::swap;
		swap(stream, src.stream);
		return *this;
	}

	operator bool() const noexcept {
		return stream != nullptr;
	}

	Istream *Steal() noexcept {
		return std::exchange(stream, nullptr);
	}

	/**
	 * This is a kludge to allow checking and inspecting a specific
	 * #Istream implementation.  Use with care.
	 */
	template<typename T>
	T *DynamicCast() noexcept {
		return dynamic_cast<T *>(stream);
	}

	/**
	 * Like DynamicCast(), but omits the RTTI check.
	 */
	template<typename T>
	T &StaticCast() noexcept {
		assert(DynamicCast<T>() != nullptr);

		return *static_cast<T *>(stream);
	}

	void Clear() noexcept {
		auto *s = Steal();
		if (s != nullptr)
			Close(*s);
	}

	[[gnu::pure]]
	IstreamLength GetLength() const noexcept;

private:
	static void Close(Istream &i) noexcept;
};

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

#pragma once

#include "util/Compiler.h"

#include <utility>

#include <assert.h>
#include <sys/types.h>

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

	gcc_pure
	off_t GetAvailable(bool partial) const noexcept;

	/**
	 * Calls Istream::AsFd().  On successful (non-negative) return
	 * value, this object is cleared.
	 */
	int AsFd() noexcept;

private:
	static void Close(Istream &i) noexcept;
};

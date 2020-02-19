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

#include <utility>

/**
 * A generic object which is owned by somebody who doesn't know how to
 * dispose it; to do this, a function pointer for disposing it is
 * provided.  Some implementations may do "delete this", but others
 * may be allocated from a custom allocator and may need different
 * ways to dispose it.
 */
class DisposablePointer {
public:
	using DisposeFunction = void(*)(void *ptr) noexcept;

private:
	void *ptr = nullptr;

	DisposeFunction dispose;

public:
	DisposablePointer() = default;
	DisposablePointer(std::nullptr_t) noexcept {}

	DisposablePointer(void *_ptr, DisposeFunction _dispose) noexcept
		:ptr(_ptr), dispose(_dispose) {}

	DisposablePointer(DisposablePointer &&src) noexcept
		:ptr(std::exchange(src.ptr, nullptr)), dispose(src.dispose) {}

	~DisposablePointer() noexcept {
		if (ptr != nullptr)
			dispose(ptr);
	}

	DisposablePointer &operator=(DisposablePointer &&other) noexcept {
		using std::swap;
		swap(ptr, other.ptr);
		swap(dispose, other.dispose);
		return *this;
	}

	operator bool() const noexcept {
		return ptr != nullptr;
	}

	void *get() const noexcept {
		return ptr;
	}

	void reset() noexcept {
		if (ptr != nullptr)
			dispose(std::exchange(ptr, nullptr));
	}
};

template<typename T>
class TypedDisposablePointer : public DisposablePointer {
public:
	template<typename... Args>
	TypedDisposablePointer(Args&&... args) noexcept
		:DisposablePointer(std::forward<Args>(args)...) {}

	TypedDisposablePointer(void *_ptr, DisposeFunction _dispose) noexcept;

	TypedDisposablePointer(T *_ptr, DisposeFunction _dispose) noexcept
		:DisposablePointer(_ptr, _dispose) {}

	T *get() const noexcept {
		return (T *)DisposablePointer::get();
	}

	T *operator->() const noexcept {
		return get();
	}

	T &operator*() const noexcept {
		return *get();
	}
};

inline DisposablePointer
ToNopPointer(void *ptr) noexcept
{
	return {ptr, [](void *) noexcept {}};
}

template<typename T>
TypedDisposablePointer<T>
ToDeletePointer(T *ptr) noexcept
{
	return {ptr, [](void *p) noexcept {
		T *t = (T *)p;
		delete t;
	}};
}

template<typename T>
TypedDisposablePointer<T>
ToDestructPointer(T *ptr) noexcept
{
	return {ptr, [](void *p) noexcept {
		T *t = (T *)p;
		t->~T();
	}};
}

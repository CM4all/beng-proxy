/*
 * Copyright (C) 2013 Max Kellermann <max@duempel.org>
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

#ifndef CAST_HXX
#define CAST_HXX

#include <stddef.h>

/**
 * Offset the given pointer by the specified number of bytes.
 */
static inline constexpr void *
OffsetPointer(void *p, ptrdiff_t offset)
{
	return (char *)p + offset;
}

/**
 * Offset the given pointer by the specified number of bytes.
 */
static inline constexpr const void *
OffsetPointer(const void *p, ptrdiff_t offset)
{
	return (const char *)p + offset;
}

template<typename T, typename U>
static inline constexpr T *
OffsetCast(U *p, ptrdiff_t offset)
{
	return reinterpret_cast<T *>(OffsetPointer(p, offset));
}

template<typename T, typename U>
static inline constexpr T *
OffsetCast(const U *p, ptrdiff_t offset)
{
	return reinterpret_cast<const T *>(OffsetPointer(p, offset));
}

/**
 * Cast the given pointer to a struct member to its parent structure.
 */
#define ContainerCast(p, container, attribute) \
	OffsetCast<container, decltype(((container*)nullptr)->attribute)> \
	((p), -ptrdiff_t(offsetof(container, attribute)))

#endif

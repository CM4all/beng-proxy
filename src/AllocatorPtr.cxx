/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "AllocatorPtr.hxx"
#include "pool/PSocketAddress.hxx"
#include "util/StringView.hxx"

std::span<const std::byte>
AllocatorPtr::Dup(std::span<const std::byte> src) const noexcept
{
	if (src.data() == nullptr)
		return {};

	if (src.empty())
		return {(const std::byte *)"", 0};

	return {(const std::byte *)Dup(src.data(), src.size()), src.size()};
}

StringView
AllocatorPtr::Dup(StringView src) const noexcept
{
	if (src == nullptr)
		return nullptr;

	if (src.empty())
		return "";

	return {(const char *)Dup(src.data, src.size), src.size};
}

const char *
AllocatorPtr::DupZ(StringView src) const noexcept
{
	if (src == nullptr)
		return nullptr;

	if (src.empty())
		return "";

	return p_strndup(&pool, src.data, src.size);
}

SocketAddress
AllocatorPtr::Dup(SocketAddress src) const noexcept
{
	return DupAddress(pool, src);
}

std::span<const std::byte>
AllocatorPtr::LazyConcat(std::span<const std::byte> a,
			 std::span<const std::byte> b) const noexcept
{
	if (a.empty())
		/* no need to allocate a new buffer */
		return b;

	if (b.empty())
		/* no need to allocate a new buffer */
		return a;

	std::size_t size = a.size() + b.size();
	std::byte *result = NewArray<std::byte>(size);
	std::copy(b.begin(), b.end(),
		  std::copy(a.begin(), a.end(), result));
	return { result, size };
}

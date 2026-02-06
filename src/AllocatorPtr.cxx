// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AllocatorPtr.hxx"
#include "pool/PSocketAddress.hxx"
#include "util/SpanCast.hxx"
#include "util/StringWithHash.hxx"

using std::string_view_literals::operator""sv;

std::span<const std::byte>
AllocatorPtr::Dup(std::span<const std::byte> src) const noexcept
{
	if (src.data() == nullptr)
		return {};

	if (src.empty())
		return {(const std::byte *)"", 0};

	return {(const std::byte *)Dup(src.data(), src.size()), src.size()};
}

std::string_view
AllocatorPtr::Dup(std::string_view src) const noexcept
{
	return ToStringView(Dup(AsBytes(src)));
}

const char *
AllocatorPtr::DupZ(std::string_view src) const noexcept
{
	if (src.data() == nullptr)
		return {};

	if (src.empty())
		return "";

	return p_strndup(&pool, src.data(), src.size());
}

StringWithHash
AllocatorPtr::Dup(StringWithHash src) const noexcept
{
	return StringWithHash{Dup(src.value), src.hash};
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

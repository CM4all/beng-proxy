// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ExpansibleBuffer.hxx"
#include "pool/pool.hxx"
#include "util/Poison.hxx"
#include "util/SpanCast.hxx"

#include <algorithm>
#include <cassert>

using std::string_view_literals::operator""sv;

ExpansibleBuffer::ExpansibleBuffer(struct pool &_pool,
				   size_t initial_size,
				   size_t _hard_limit) noexcept
	:pool(_pool),
	 buffer(PoolAlloc<std::byte>(pool, initial_size)),
	 hard_limit(_hard_limit),
	 max_size(initial_size)
{
	assert(initial_size > 0);
	assert(hard_limit >= initial_size);
}

void
ExpansibleBuffer::Clear() noexcept
{
	PoisonUndefined(buffer, max_size);

	size = 0;
}

bool
ExpansibleBuffer::Resize(size_t new_max_size) noexcept
{
	assert(new_max_size > max_size);

	if (new_max_size > hard_limit)
		return false;

	std::byte *new_buffer = PoolAlloc<std::byte>(pool, new_max_size);
	std::copy_n(buffer, size, new_buffer);

	p_free(&pool, buffer, max_size);

	buffer = new_buffer;
	max_size = new_max_size;
	return true;
}

std::byte *
ExpansibleBuffer::BeginWrite(std::size_t add_size) noexcept
{
	size_t new_size = size + add_size;
	if (new_size > max_size &&
	    !Resize(((new_size - 1) | 0x3ff) + 1))
		return nullptr;

	return buffer + size;
}

bool
ExpansibleBuffer::Write(std::span<const std::byte> src) noexcept
{
	std::byte *q = BeginWrite(src.size());
	if (q == nullptr)
		return false;

	std::copy(src.begin(), src.end(), q);
	CommitWrite(src.size());
	return true;
}

bool
ExpansibleBuffer::Write(std::string_view src) noexcept
{
	return Write(AsBytes(src));
}

bool
ExpansibleBuffer::Set(std::span<const std::byte> src) noexcept
{
	if (src.size() > max_size && !Resize(((src.size() - 1) | 0x3ff) + 1))
		return false;

	size = src.size();
	memcpy(buffer, src.data(), src.size());
	return true;
}

bool
ExpansibleBuffer::Set(std::string_view p) noexcept
{
	return Set(AsBytes(p));
}

std::span<const std::byte>
ExpansibleBuffer::Read() const noexcept
{
	return {(const std::byte *)buffer, size};
}

const char *
ExpansibleBuffer::ReadString() noexcept
{
	if (size == 0 || buffer[size - 1] != std::byte{})
		/* append a null terminator */
		Write("\0"sv);

	/* the buffer is now a valid C string (assuming it doesn't contain
	   any nulls */
	return reinterpret_cast<const char *>(buffer);
}

std::string_view
ExpansibleBuffer::ReadStringView() const noexcept
{
	return { (const char *)buffer, size };
}

std::span<std::byte>
ExpansibleBuffer::Dup(struct pool &_pool) const noexcept
{
	return {
		(std::byte *)p_memdup(&_pool, buffer, size),
		size,
	};
}

char *
ExpansibleBuffer::StringDup(struct pool &_pool) const noexcept
{
	char *p = (char *)p_malloc(&_pool, size + 1);
	memcpy(p, buffer, size);
	p[size] = 0;
	return p;
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "load_file.hxx"
#include "HttpMessageResponse.hxx"
#include "pool/pool.hxx"
#include "lib/fmt/SystemError.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "http/Status.hxx"

std::span<const std::byte>
LoadFile(struct pool &pool, const char *path, off_t max_size)
{
	auto fd = OpenReadOnly(path);

	off_t size = fd.GetSize();
	if (size < 0)
		throw FmtErrno("Failed to stat '{}'", path);

	if (size > max_size)
		throw HttpMessageResponse(HttpStatus::INTERNAL_SERVER_ERROR,
					  FmtBuffer<256>("File is too large: {}", path));

	if (size == 0)
		return { (const std::byte *)"", 0 };

	std::byte *p = PoolAlloc<std::byte>(pool, size);
	if (p == nullptr)
		throw std::bad_alloc();

	ssize_t nbytes = fd.Read({p, (std::size_t)size});
	if (nbytes < 0)
		throw FmtErrno("Failed to read from '{}'", path);

	if (size_t(nbytes) != size_t(size))
		throw HttpMessageResponse(HttpStatus::INTERNAL_SERVER_ERROR,
					  FmtBuffer<256>("Short read from: {}", path));

	return { p, size_t(size) };
}

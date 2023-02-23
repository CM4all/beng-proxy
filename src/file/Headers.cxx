// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Headers.hxx"
#include "strmap.hxx"
#include "pool/pool.hxx"
#include "http/Date.hxx"
#include "io/FileDescriptor.hxx"
#include "util/Base32.hxx"
#include "AllocatorPtr.hxx"

#include <attr/xattr.h>

#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>

static bool
ReadETag(FileDescriptor fd, char *buffer, size_t size) noexcept
{
	assert(fd.IsDefined());
	assert(size > 4);

	const auto nbytes = fgetxattr(fd.Get(), "user.ETag", buffer + 1, size - 3);
	if (nbytes <= 0)
		return false;

	assert((size_t)nbytes < size);

	buffer[0] = '"';
	buffer[nbytes + 1] = '"';
	buffer[nbytes + 2] = 0;
	return true;
}

static void
static_etag(char *p, const struct statx &st)
{
	*p++ = '"';

	p = FormatIntBase32(p, st.stx_dev_major);
	p = FormatIntBase32(p, st.stx_dev_minor);

	*p++ = '-';

	p = FormatIntBase32(p, st.stx_ino);

	*p++ = '-';

	p = FormatIntBase32(p, st.stx_mtime.tv_sec);

	*p++ = '-';

	p = FormatIntBase32(p, st.stx_mtime.tv_nsec);

	*p++ = '"';
	*p = 0;
}

void
GetAnyETag(char *buffer, size_t size,
	   FileDescriptor fd, const struct statx &st) noexcept
{
	if (!fd.IsDefined() || !ReadETag(fd, buffer, size))
		static_etag(buffer, st);
}

bool
load_xattr_content_type(char *buffer, size_t size, FileDescriptor fd) noexcept
{
	if (!fd.IsDefined())
		return false;

	ssize_t nbytes = fgetxattr(fd.Get(), "user.Content-Type",
				   buffer, size - 1);
	if (nbytes <= 0)
		return false;

	assert((size_t)nbytes < size);
	buffer[nbytes] = 0;
	return true;
}

StringMap
static_response_headers(struct pool &pool,
			FileDescriptor fd, const struct statx &st,
			const char *content_type)
{
	StringMap headers;

	if (S_ISCHR(st.stx_mode))
		return headers;

	char buffer[256];

	if (content_type == nullptr)
		content_type = load_xattr_content_type(buffer, sizeof(buffer), fd)
			? p_strdup(&pool, buffer)
			: "application/octet-stream";

	headers.Add(pool, "content-type", content_type);

	headers.Add(pool, "last-modified",
		    p_strdup(&pool, http_date_format(std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec))));

	GetAnyETag(buffer, sizeof(buffer), fd, st);
	headers.Add(pool, "etag", p_strdup(&pool, buffer));

	return headers;
}

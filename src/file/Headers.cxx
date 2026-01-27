// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Headers.hxx"
#include "strmap.hxx"
#include "pool/pool.hxx"
#include "http/CommonHeaders.hxx"
#include "http/Date.hxx"
#include "io/FileDescriptor.hxx"
#include "util/Base32.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>

static constexpr void
static_etag(char *p, const struct statx &st) noexcept
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
GetAnyETag(char *buffer,
	   const struct statx &st) noexcept
{
	static_etag(buffer, st);
}

StringMap
static_response_headers(struct pool &pool,
			const struct statx &st,
			const char *content_type) noexcept
{
	assert(S_ISREG(st.stx_mode));

	StringMap headers;

	char buffer[256];

	if (content_type == nullptr)
		content_type = "application/octet-stream";

	headers.Add(pool, content_type_header, content_type);

	headers.Add(pool, last_modified_header,
		    p_strdup(&pool, http_date_format(std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec))));

	GetAnyETag(buffer, st);
	headers.Add(pool, etag_header, p_strdup(&pool, buffer));

	return headers;
}

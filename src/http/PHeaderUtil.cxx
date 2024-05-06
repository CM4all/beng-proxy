// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PHeaderUtil.hxx"
#include "CommonHeaders.hxx"
#include "http/List.hxx"
#include "http/Date.hxx"
#include "strmap.hxx"

int
http_client_accepts_encoding(const StringMap &request_headers,
			     const char *coding) noexcept
{
	const char *accept_encoding = request_headers.Get(accept_encoding_header);
	return accept_encoding != nullptr &&
		http_list_contains(accept_encoding, coding);
}

std::chrono::system_clock::time_point
GetServerDate(const StringMap &response_headers) noexcept
{
	const char *p = response_headers.Get(date_header);
	if (p == nullptr)
		/* server does not provide its system time */
		return std::chrono::system_clock::from_time_t(-1);

	return http_date_parse(p);

}

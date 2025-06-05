// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/Status.hxx"

constexpr bool
FilterStatusIsRecessive(HttpStatus status) noexcept
{
	return status == HttpStatus::OK || status == HttpStatus::NO_CONTENT;
}

constexpr HttpStatus
ApplyFilterStatus(HttpStatus previous_status, HttpStatus filter_status,
		  bool has_body) noexcept
{
	/* if the filter didn't specify a status (other than 200 or
	   204), forward the previous status instead */
	return FilterStatusIsRecessive(filter_status) &&
		/* ... but only if it is compatible with the presence of a
		   response body */
		(!http_status_is_empty(previous_status) || !has_body)
		? previous_status
		: filter_status;
}

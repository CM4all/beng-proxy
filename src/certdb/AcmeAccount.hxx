// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <forward_list>
#include <string>
#include <string_view>

struct AcmeAccount {
	enum class Status {
		VALID,
		DEACTIVATED,
		REVOKED,
	} status = Status::VALID;

	std::string location;

	std::forward_list<std::string> contact;

	/**
	 * Return an arbitrary email address from the "contact" array.
	 * The "mailto:" prefix is stripped.  The returned pointer is
	 * owned by this object.  Returns nullptr if no email address
	 * is present.
	 */
	const char *GetEmail() const noexcept;

	static Status ParseStatus(const std::string_view s);
	static const char *FormatStatus(Status s) noexcept;
};

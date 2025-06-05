// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <exception>
#include <string>
#include <string_view>

class AcmeError;

struct AcmeChallenge {
	std::string type;

	enum class Status {
		PENDING,
		PROCESSING,
		VALID,
		INVALID,
	} status = Status::INVALID;

	std::string token;
	std::string uri;

	std::exception_ptr error;

	void Check() const;

	static Status ParseStatus(std::string_view s);
	static const char *FormatStatus(Status s) noexcept;
};

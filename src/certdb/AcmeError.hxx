// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <boost/json/fwd.hpp>

#include <stdexcept>
#include <string>

class AcmeError : public std::runtime_error {
	std::string type;

public:
	AcmeError(const boost::json::object &error);

	const std::string &GetType() const {
		return type;
	}
};

[[gnu::pure]]
bool
IsAcmeErrorType(std::exception_ptr ep, const char *type) noexcept;

[[gnu::pure]]
bool
IsAcmeUnauthorizedError(std::exception_ptr ep) noexcept;

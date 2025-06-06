// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <nlohmann/json_fwd.hpp>

#include <exception> // for std::exception_ptr
#include <stdexcept>
#include <string>

class AcmeError : public std::runtime_error {
	std::string type;

public:
	explicit AcmeError(const nlohmann::json &error);

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

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <stdexcept>

class FcgiClientError : public std::runtime_error {
public:
	explicit FcgiClientError(const char *_msg)
		:std::runtime_error(_msg) {}
};

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <stdexcept>

class FcgiClientError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

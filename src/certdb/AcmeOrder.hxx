// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>
#include <forward_list>

struct AcmeOrder {
	std::string location;
	std::string status;
	std::forward_list<std::string> authorizations;
	std::string finalize;
	std::string certificate;
};

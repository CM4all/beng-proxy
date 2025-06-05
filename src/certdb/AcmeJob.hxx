// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string>

struct AcmeJobRequest {
	std::forward_list<std::string> identifiers;
};

struct AcmeJobResponse {
	std::string status;
	std::forward_list<std::string> authorizations;
	std::string finalize;
};

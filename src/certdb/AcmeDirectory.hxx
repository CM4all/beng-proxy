// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string>

struct AcmeDirectory {
	std::string new_nonce;
	std::string new_account;
	std::string new_order;
};

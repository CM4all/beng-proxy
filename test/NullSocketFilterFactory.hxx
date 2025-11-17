// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "fs/Factory.hxx"

class NullSocketFilterFactory final : public SocketFilterFactory {
public:
	SocketFilterPtr CreateFilter() override {
		return {};
	}
};

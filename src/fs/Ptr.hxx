// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <memory>

class SocketFilter;

class SocketFilterDisposer {
public:
	void operator()(SocketFilter *f) const noexcept;
};

using SocketFilterPtr = std::unique_ptr<SocketFilter, SocketFilterDisposer>;

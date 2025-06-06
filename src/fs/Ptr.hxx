// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <memory>

class SocketFilter;
class SocketFilterFactory;

class SocketFilterDisposer {
public:
	void operator()(SocketFilter *f) const noexcept;
};

using SocketFilterPtr = std::unique_ptr<SocketFilter, SocketFilterDisposer>;

using SocketFilterFactoryPtr = std::unique_ptr<SocketFilterFactory>;

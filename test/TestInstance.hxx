// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "PInstance.hxx"

class AutoPoolCommit {
public:
	~AutoPoolCommit() noexcept;
};

struct TestInstance : AutoPoolCommit, PInstance {
	TestInstance() noexcept;
	~TestInstance() noexcept;
};

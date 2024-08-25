// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stock/PutAction.hxx"

#include <cstdint>

class WasLease {
public:
	virtual void ReleaseWas(PutAction action) noexcept = 0;
	virtual void ReleaseWasStop(uint_least64_t input_received) noexcept = 0;
};

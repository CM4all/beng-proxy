// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "stock/PutAction.hxx"

#include <cstdint>

class WasLease {
public:
	virtual PutAction ReleaseWas(PutAction action) noexcept = 0;
	virtual PutAction ReleaseWasStop(uint_least64_t input_received) noexcept = 0;
};

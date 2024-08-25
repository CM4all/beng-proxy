// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stock/PutAction.hxx"

#include <stdint.h>

class WasLease {
public:
	virtual void ReleaseWas(PutAction action) noexcept = 0;
	virtual void ReleaseWasStop(uint64_t input_received) noexcept = 0;
};

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "lease.hxx"

struct RecordingLease : Lease {
	bool released = false;
	PutAction action;

	void ReleaseLease(PutAction _action) noexcept override {
		released = true;
		action = _action;
	}
};

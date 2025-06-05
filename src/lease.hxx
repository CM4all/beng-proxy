// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "stock/PutAction.hxx"

#include <assert.h>

class Lease {
public:
	virtual PutAction ReleaseLease(PutAction action) noexcept = 0;
};

class LeasePtr {
	Lease *lease = nullptr;

public:
	LeasePtr() = default;

	explicit LeasePtr(Lease &_lease) noexcept
		:lease(&_lease) {}

	~LeasePtr() noexcept {
		assert(lease == nullptr);
	}

	LeasePtr(const LeasePtr &) = delete;
	LeasePtr &operator=(const LeasePtr &) = delete;

	operator bool() const noexcept {
		return lease != nullptr;
	}

	void Set(Lease &_lease) noexcept {
		lease = &_lease;
	}

	PutAction Release(PutAction action) noexcept {
		assert(lease != nullptr);

		auto *l = lease;
		lease = nullptr;
		return l->ReleaseLease(action);
	}
};

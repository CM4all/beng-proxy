// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <assert.h>

class Lease {
public:
	virtual void ReleaseLease(bool reuse) noexcept = 0;
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

	void Release(bool reuse) noexcept {
		assert(lease != nullptr);

		auto *l = lease;
		lease = nullptr;
		l->ReleaseLease(reuse);
	}
};

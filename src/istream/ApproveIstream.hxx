// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "pool/SharedPtr.hxx"

#include <sys/types.h>

struct pool;
class EventLoop;
class UnusedIstreamPtr;
class ApproveIstream;

class ApproveIstreamControl {
	friend class ApproveIstream;

	ApproveIstream *approve;

public:
	explicit constexpr ApproveIstreamControl(ApproveIstream &_approve) noexcept
		:approve(&_approve) {}

	void Approve(off_t nbytes) noexcept;
};

/**
 * This #Istream filter passes only data after it has been approved.
 */
std::pair<UnusedIstreamPtr, SharedPoolPtr<ApproveIstreamControl>>
NewApproveIstream(struct pool &pool, EventLoop &event_loop,
		  UnusedIstreamPtr input);

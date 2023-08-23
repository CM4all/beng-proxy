// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ThreadSocketFilter.hxx"

#include <atomic>
#include <mutex>
#include <condition_variable>

/**
 * A #ThreadSocketFilterHandler implementation which just passes all
 * data through as-is (like #NopThreadSocketFilter), but blocks
 * until approval is given.  This is useful to reproduce race
 * conditions in unit tests.
 */
class ApproveThreadSocketFilter final : public ThreadSocketFilterHandler {
	SliceFifoBuffer input;

	std::mutex mutex;
	std::condition_variable cond;

	std::atomic_size_t approved = 0;

	bool busy = false, cancel = false;

public:
	void Approve(std::size_t nbytes) noexcept;

	/* virtual methods from class ThreadSocketFilterHandler */
	void PreRun(ThreadSocketFilterInternal &) noexcept override;
	void Run(ThreadSocketFilterInternal &f) override;
	void CancelRun(ThreadSocketFilterInternal &) noexcept override;

private:
	std::size_t WaitForApproval() noexcept;
};

/*
 * Copyright 2007-2022 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "ThreadSocketFilter.hxx"

#include <atomic>
#include <mutex>
#include <condition_variable>

/**
 * A #ThreadSocketFilterHandler implementation which just passes all
 * data through as-is (like #NopThreadSocketFilter), but needs blocks
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

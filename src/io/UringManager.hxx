/*
 * Copyright 2020 CM4all GmbH
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

#include "io/uring/Queue.hxx"
#include "event/DeferEvent.hxx"
#include "event/SocketEvent.hxx"

class UringManager : public Uring::Queue {
	SocketEvent event;

	/**
	 * Responsible for invoking Uring::Queue::Submit() only once
	 * per #EventLoop iteration.
	 */
	DeferEvent defer_submit_event;

	bool volatile_event = false;

public:
	explicit UringManager(EventLoop &event_loop)
		:Uring::Queue(1024, 0),
		 event(event_loop, BIND_THIS_METHOD(OnReady),
		       SocketDescriptor::FromFileDescriptor(GetFileDescriptor())),
		 defer_submit_event(event_loop,
				    BIND_THIS_METHOD(DeferredSubmit))
	{
		event.ScheduleRead();
	}

	void SetVolatile() noexcept {
		volatile_event = true;
		CheckVolatileEvent();
	}

	void AddPending(struct io_uring_sqe &sqe,
			Uring::Operation &operation) noexcept {
		Uring::Queue::AddPending(sqe, operation);
		defer_submit_event.Schedule();
	}

private:
	void CheckVolatileEvent() noexcept {
		if (volatile_event && !HasPending())
			event.Cancel();
	}

	void OnReady(unsigned events) noexcept;
	void DeferredSubmit() noexcept;
};

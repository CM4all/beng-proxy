// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "fs/Factory.hxx"
#include "fs/NopThreadSocketFilter.hxx"
#include "fs/ThreadSocketFilter.hxx"
#include "thread/Pool.hxx"
#include "thread/Queue.hxx"

class NopThreadSocketFilterFactory final : public SocketFilterFactory {
	EventLoop &event_loop;

public:
	explicit NopThreadSocketFilterFactory(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {
		/* keep the eventfd unregistered if the ThreadQueue is
		   empty, so EventLoop::Dispatch() doesn't keep
		   running after the HTTP request has completed */
		thread_pool_set_volatile();
	}

	~NopThreadSocketFilterFactory() noexcept override {
		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	SocketFilterPtr CreateFilter() override {
		return SocketFilterPtr{
			new ThreadSocketFilter(thread_pool_get_queue(event_loop),
					       std::make_unique<NopThreadSocketFilter>())
		};
	}

	void Flush() noexcept {
		thread_pool_get_queue(event_loop).FlushSynchronously();
	}
};

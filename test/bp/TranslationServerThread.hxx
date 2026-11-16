// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "translation/server/Listener.hxx"
#include "event/Loop.hxx"
#include "event/PipeEvent.hxx"

#include <pthread.h>

class TranslationServerThread {
	EventLoop event_loop;

	PipeEvent stop_event;

	Translation::Server::Listener listener;

	pthread_t thread;

public:
	TranslationServerThread(UniqueSocketDescriptor &&socket, Translation::Server::Handler &handler);
	~TranslationServerThread() noexcept;

private:
	void OnStop(unsigned) noexcept {
		event_loop.Break();
	}

	void Run() noexcept {
		stop_event.ScheduleRead();
		event_loop.Run();
	}

	static void *Run(void *p) noexcept {
		auto &t = *static_cast<TranslationServerThread *>(p);
		t.Run();
		return nullptr;
	}
};

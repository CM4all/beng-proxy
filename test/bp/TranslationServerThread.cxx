// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TranslationServerThread.hxx"
#include "system/Error.hxx"
#include "system/LinuxFD.hxx"
#include "util/SpanCast.hxx"

TranslationServerThread::TranslationServerThread(UniqueSocketDescriptor &&socket, Translation::Server::Handler &handler)
	:stop_event(event_loop, BIND_THIS_METHOD(OnStop), CreateEventFD().Release()),
	 listener(event_loop, handler)
{
	listener.Listen(std::move(socket));

	if (int error = pthread_create(&thread, nullptr, Run, this);
	    error != 0)
		throw MakeErrno(error, "pthread_create() failed");
}

TranslationServerThread::~TranslationServerThread() noexcept
{
	static constexpr uint64_t value = 1;
	(void)stop_event.GetFileDescriptor()
		.Write(ReferenceAsBytes(value));
	pthread_join(thread, nullptr);
}

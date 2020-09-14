/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "sink_fd.hxx"
#include "Sink.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "io/Splice.hxx"
#include "io/SpliceSupport.hxx"
#include "io/FileDescriptor.hxx"
#include "event/SocketEvent.hxx"
#include "util/DestructObserver.hxx"
#include "util/LeakDetector.hxx"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

class SinkFd final : IstreamSink, DestructAnchor, LeakDetector {
	FileDescriptor fd;
	const FdType fd_type;
	SinkFdHandler &handler;

	SocketEvent event;

	/**
	 * Set to true each time data was received from the istream.
	 */
	bool got_data;

	/**
	 * This flag is used to determine if the SocketEvent::WRITE event
	 * shall be scheduled after a splice().  We need to add the event
	 * only if the splice() was triggered by SocketEvent::WRITE,
	 * because then we're responsible for querying more data.
	 */
	bool got_event = false;

#ifndef NDEBUG
	bool valid = true;
#endif

public:
	SinkFd(EventLoop &event_loop,
	       UnusedIstreamPtr &&_istream,
	       FileDescriptor _fd, FdType _fd_type,
	       SinkFdHandler &_handler) noexcept
		:IstreamSink(std::move(_istream)),
		 fd(_fd), fd_type(_fd_type),
		 handler(_handler),
		 event(event_loop, BIND_THIS_METHOD(EventCallback),
		       SocketDescriptor::FromFileDescriptor(fd))
	{
		input.SetDirect(istream_direct_mask_to(fd_type));

		ScheduleWrite();
	}

	void Destroy() noexcept {
		this->~SinkFd();
	}

	bool IsDefined() const noexcept {
		return input.IsDefined();
	}

	void Read() noexcept {
		assert(valid);
		assert(IsDefined());

		input.Read();
	}

	void Close() noexcept {
#ifndef NDEBUG
		valid = false;
#endif

		input.ClearAndClose();
		Destroy();
	}

private:
	void ScheduleWrite() noexcept {
		assert(fd.IsDefined());
		assert(input.IsDefined());

		got_event = false;
		event.ScheduleWrite();
	}

	void EventCallback(unsigned events) noexcept;

	/* virtual methods from class IstreamHandler */
	size_t OnData(const void *data, size_t length) noexcept override;
	ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

/*
 * istream handler
 *
 */

size_t
SinkFd::OnData(const void *data, size_t length) noexcept
{
	got_data = true;

	ssize_t nbytes = IsAnySocket(fd_type)
		? send(fd.Get(), data, length, MSG_DONTWAIT|MSG_NOSIGNAL)
		: fd.Write(data, length);
	if (nbytes >= 0) {
		ScheduleWrite();
		return nbytes;
	} else if (errno == EAGAIN) {
		ScheduleWrite();
		return 0;
	} else {
		event.Cancel();
		if (handler.OnSendError(errno)) {
			input.ClearAndClose();
			Destroy();
		}
		return 0;
	}
}

ssize_t
SinkFd::OnDirect(FdType type, int _fd, size_t max_length) noexcept
{
	got_data = true;

	ssize_t nbytes = SpliceTo(_fd, type, fd.Get(), fd_type, max_length);
	if (gcc_unlikely(nbytes < 0 && errno == EAGAIN)) {
		if (!fd.IsReadyForWriting()) {
			ScheduleWrite();
			return ISTREAM_RESULT_BLOCKING;
		}

		/* try again, just in case connection->fd has become ready
		   between the first istream_direct_to_socket() call and
		   fd_ready_for_writing() */
		nbytes = SpliceTo(_fd, type, fd.Get(), fd_type, max_length);
	}

	if (gcc_likely(nbytes > 0) && (got_event || type == FdType::FD_FILE))
		/* regular files don't have support for SocketEvent::READ, and
		   thus the sink is responsible for triggering the next
		   splice */
		ScheduleWrite();

	return nbytes;
}

void
SinkFd::OnEof() noexcept
{
	got_data = true;

#ifndef NDEBUG
	valid = false;
#endif

	event.Cancel();

	handler.OnInputEof();
	Destroy();
}

void
SinkFd::OnError(std::exception_ptr ep) noexcept
{
	got_data = true;

#ifndef NDEBUG
	valid = false;
#endif

	event.Cancel();

	handler.OnInputError(ep);
	Destroy();
}

/*
 * libevent callback
 *
 */

inline void
SinkFd::EventCallback(unsigned) noexcept
{
	const DestructObserver destructed(*this);

	got_event = true;
	got_data = false;
	input.Read();

	if (!destructed && !got_data)
		/* the fd is ready for writing, but the istream is blocking -
		   don't try again for now */
		event.Cancel();
}

/*
 * constructor
 *
 */

SinkFd *
sink_fd_new(EventLoop &event_loop, struct pool &pool, UnusedIstreamPtr istream,
	    FileDescriptor fd, FdType fd_type,
	    SinkFdHandler &handler) noexcept
{
	assert(fd.IsDefined());

	return NewFromPool<SinkFd>(pool, event_loop, std::move(istream),
				   fd, fd_type,
				   handler);
}

void
sink_fd_read(SinkFd *ss) noexcept
{
	assert(ss != nullptr);

	ss->Read();
}

void
sink_fd_close(SinkFd *ss) noexcept
{
	assert(ss != nullptr);

	ss->Close();
}

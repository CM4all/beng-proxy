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

#include "IstreamSpawn.hxx"
#include "spawn/Interface.hxx"
#include "spawn/ProcessHandle.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ExitListener.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/Sink.hxx"
#include "io/Splice.hxx"
#include "io/SpliceSupport.hxx"
#include "io/Buffered.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/Logger.hxx"
#include "net/SocketDescriptor.hxx"
#include "event/PipeEvent.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "system/Error.hxx"

#include <cassert>
#include <memory>

#ifdef __linux
#include <fcntl.h>
#endif

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <limits.h>

struct SpawnIstream final : Istream, IstreamSink, ExitListener {
	const LLogger logger;

	UniqueFileDescriptor output_fd;
	PipeEvent output_event;

	SliceFifoBuffer buffer;

	UniqueFileDescriptor input_fd;
	PipeEvent input_event;

	std::unique_ptr<ChildProcessHandle> handle;

	bool direct = false;

	SpawnIstream(EventLoop &event_loop,
		     struct pool &p,
		     UnusedIstreamPtr _input, UniqueFileDescriptor _input_fd,
		     UniqueFileDescriptor _output_fd,
		     std::unique_ptr<ChildProcessHandle> &&_handle) noexcept;

	bool CheckDirect() const noexcept {
		return direct;
	}

	void Cancel() noexcept;

	void FreeBuffer() noexcept {
		buffer.FreeIfDefined();
	}

	/**
	 * Send data from the buffer.  Invokes the "eof" callback when the
	 * buffer becomes empty and the pipe has been closed already.
	 *
	 * @return true if the caller shall read more data from the pipe
	 */
	bool SendFromBuffer() noexcept;

	void ReadFromOutput() noexcept;

	void InputEventCallback(unsigned) noexcept {
		input_event.Cancel(); // TODO: take advantage of EV_PERSIST

		input.Read();
	}

	void OutputEventCallback(unsigned) noexcept {
		output_event.Cancel(); // TODO: take advantage of EV_PERSIST

		ReadFromOutput();
	}

	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct = (mask & ISTREAM_TO_PIPE) != 0;
	}

	void _Read() noexcept override;

	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	// TODO: implement int AsFd() override;
	void _Close() noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(const void *data, std::size_t length) noexcept override;
	IstreamDirectResult OnDirect(FdType type, int fd,
				     std::size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class ExitListener */
	void OnChildProcessExit(int status) noexcept override;
};

void
SpawnIstream::Cancel() noexcept
{
	assert(output_fd.IsDefined());

	if (HasInput()) {
		assert(input_fd.IsDefined());

		input_event.Cancel();
		input_fd.Close();
		CloseInput();
	}

	output_event.Cancel();

	output_fd.Close();

	handle.reset();
}

inline bool
SpawnIstream::SendFromBuffer() noexcept
{
	assert(buffer.IsDefined());

	if (Istream::SendFromBuffer(buffer) == 0)
		return false;

	if (!output_fd.IsDefined()) {
		if (buffer.empty()) {
			FreeBuffer();
			DestroyEof();
		}

		return false;
	}

	buffer.FreeIfEmpty();

	return true;
}

/*
 * input handler
 *
 */

std::size_t
SpawnIstream::OnData(const void *data, std::size_t length) noexcept
{
	assert(input_fd.IsDefined());

	ssize_t nbytes = input_fd.Write(data, length);
	if (nbytes > 0)
		input_event.ScheduleWrite();
	else if (nbytes < 0) {
		if (errno == EAGAIN) {
			input_event.ScheduleWrite();
			return 0;
		}

		logger(1, "write() to subprocess failed: ", strerror(errno));
		input_event.Cancel();
		input_fd.Close();
		CloseInput();
		return 0;
	}

	return (std::size_t)nbytes;
}

IstreamDirectResult
SpawnIstream::OnDirect(gcc_unused FdType type, int fd, std::size_t max_length) noexcept
{
	assert(input_fd.IsDefined());

	ssize_t nbytes = SpliceToPipe(fd, input_fd.Get(), max_length);
	if (nbytes <= 0) {
		if (nbytes == 0)
			return IstreamDirectResult::END;

		if (errno != EAGAIN)
			return IstreamDirectResult::ERRNO;

		if (!input_fd.IsReadyForWriting()) {
			input_event.ScheduleWrite();
			return IstreamDirectResult::BLOCKING;
		}

		/* try again, just in case connection->fd has become
		   ready between the first splice() call and
		   fd_ready_for_writing() */
		nbytes = SpliceToPipe(fd, input_fd.Get(), max_length);
		if (nbytes <= 0)
			return nbytes < 0
				? IstreamDirectResult::ERRNO
				: IstreamDirectResult::END;
	}

	input.ConsumeDirect(nbytes);
	return IstreamDirectResult::OK;
}

inline void
SpawnIstream::OnEof() noexcept
{
	assert(HasInput());
	assert(input_fd.IsDefined());

	input_event.Cancel();
	input_fd.Close();

	ClearInput();
}

void
SpawnIstream::OnError(std::exception_ptr ep) noexcept
{
	assert(HasInput());
	assert(input_fd.IsDefined());

	FreeBuffer();

	input_event.Cancel();
	input_fd.Close();
	ClearInput();

	Cancel();
	DestroyError(ep);
}

/*
 * event for fork.output_fd
 */

void
SpawnIstream::ReadFromOutput() noexcept
{
	assert(output_fd.IsDefined());

	if (!CheckDirect()) {
		buffer.AllocateIfNull(fb_pool_get());

		ssize_t nbytes = read_to_buffer(output_fd.Get(), buffer, INT_MAX);
		if (nbytes == -2) {
			/* XXX should not happen */
		} else if (nbytes > 0) {
			if (Istream::SendFromBuffer(buffer) > 0) {
				buffer.FreeIfEmpty();
				output_event.ScheduleRead();
			}
		} else if (nbytes == 0) {
			Cancel();

			if (buffer.empty()) {
				FreeBuffer();
				DestroyEof();
			}
		} else if (errno == EAGAIN) {
			buffer.FreeIfEmpty();
			output_event.ScheduleRead();

			if (HasInput())
				/* the CGI may be waiting for more data from stdin */
				input.Read();
		} else {
			auto error = MakeErrno("failed to read from sub process");
			FreeBuffer();
			Cancel();
			DestroyError(std::make_exception_ptr(error));
		}
	} else {
		if (Istream::ConsumeFromBuffer(buffer) > 0)
			/* there's data left in the buffer, which must be consumed
			   before we can switch to "direct" transfer */
			return;

		buffer.FreeIfDefined();

		/* at this point, the handler might have changed inside
		   Istream::ConsumeFromBuffer(), and the new handler might not
		   support "direct" transfer - check again */
		if (!CheckDirect()) {
			output_event.ScheduleRead();
			return;
		}

		switch (InvokeDirect(FdType::FD_PIPE, output_fd.Get(),
				     INT_MAX)) {
		case IstreamDirectResult::BLOCKING:
		case IstreamDirectResult::CLOSED:
			break;

		case IstreamDirectResult::OK:
			output_event.ScheduleRead();
			break;

		case IstreamDirectResult::END:
			FreeBuffer();
			Cancel();
			DestroyEof();
			break;

		case IstreamDirectResult::ERRNO:
			if (errno == EAGAIN) {
				output_event.ScheduleRead();

				if (HasInput())
					/* the CGI may be waiting for more data from stdin */
					input.Read();
			} else {
				auto error = MakeErrno("failed to read from sub process");
				FreeBuffer();
				Cancel();
				DestroyError(std::make_exception_ptr(error));
			}

			break;
		}
	}
}


/*
 * istream implementation
 *
 */

void
SpawnIstream::_Read() noexcept
{
	if (buffer.empty() || SendFromBuffer())
		ReadFromOutput();
}

void
SpawnIstream::_ConsumeDirect(std::size_t) noexcept
{
	output_event.ScheduleRead();
}

void
SpawnIstream::_Close() noexcept
{
	FreeBuffer();

	if (output_fd.IsDefined())
		Cancel();

	Destroy();
}

/*
 * child callback
 *
 */

void
SpawnIstream::OnChildProcessExit(gcc_unused int status) noexcept
{
	assert(handle);
	handle.reset();
}


/*
 * constructor
 *
 */

inline
SpawnIstream::SpawnIstream(EventLoop &event_loop,
			   struct pool &p,
			   UnusedIstreamPtr _input,
			   UniqueFileDescriptor _input_fd,
			   UniqueFileDescriptor _output_fd,
			   std::unique_ptr<ChildProcessHandle> &&_handle) noexcept
	:Istream(p),
	 IstreamSink(std::move(_input)),
	 logger("spawn"),
	 output_fd(std::move(_output_fd)),
	 output_event(event_loop, BIND_THIS_METHOD(OutputEventCallback),
		      output_fd),
	 input_fd(std::move(_input_fd)),
	 input_event(event_loop, BIND_THIS_METHOD(InputEventCallback),
		     input_fd),
	 handle(std::move(_handle))
{
	if (HasInput()) {
		input.SetDirect(ISTREAM_TO_PIPE);
		input_event.ScheduleWrite();
	}

	handle->SetExitListener(*this);
}

UnusedIstreamPtr
SpawnChildProcess(EventLoop &event_loop, struct pool *pool, const char *name,
		  UnusedIstreamPtr input,
		  PreparedChildProcess &&prepared,
		  SpawnService &spawn_service)
{
	if (input) {
		int fd = input.AsFd();
		if (fd >= 0)
			prepared.SetStdin(fd);
	}

	UniqueFileDescriptor stdin_pipe;
	if (input) {
		UniqueFileDescriptor stdin_r;
		if (!UniqueFileDescriptor::CreatePipe(stdin_r, stdin_pipe))
			throw MakeErrno("pipe() failed");

		prepared.SetStdin(std::move(stdin_r));

		stdin_pipe.SetNonBlocking();
	}

	UniqueFileDescriptor stdout_pipe, stdout_w;
	if (!UniqueFileDescriptor::CreatePipe(stdout_pipe, stdout_w))
		throw MakeErrno("pipe() failed");

	prepared.SetStdout(std::move(stdout_w));

	stdout_pipe.SetNonBlocking();

	auto handle = spawn_service.SpawnChildProcess(name, std::move(prepared));
	auto f = NewFromPool<SpawnIstream>(*pool, event_loop,
					   *pool,
					   std::move(input), std::move(stdin_pipe),
					   std::move(stdout_pipe),
					   std::move(handle));

	/* XXX CLOEXEC */

	return UnusedIstreamPtr(f);
}

// SPDX-License-Identifier: BSD-2-Clausey
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamSpawn.hxx"
#include "spawn/Interface.hxx"
#include "spawn/ProcessHandle.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ExitListener.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/Sink.hxx"
#include "io/Pipe.hxx"
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
	void _Close() noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
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
SpawnIstream::OnData(std::span<const std::byte> src) noexcept
{
	assert(input_fd.IsDefined());

	ssize_t nbytes = input_fd.Write(src);
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
SpawnIstream::OnDirect([[maybe_unused]] FdType type, FileDescriptor fd, off_t offset,
		       std::size_t max_length,
		       [[maybe_unused]] bool then_eof) noexcept
{
	assert(input_fd.IsDefined());

	ssize_t nbytes = SpliceToPipe(fd, ToOffsetPointer(offset),
				      input_fd, max_length);
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
		nbytes = SpliceToPipe(fd, ToOffsetPointer(offset),
				      input_fd, max_length);
		if (nbytes <= 0)
			return nbytes < 0
				? IstreamDirectResult::ERRNO
				: IstreamDirectResult::END;
	}

	input.ConsumeDirect(nbytes);

	if (!then_eof || static_cast<std::size_t>(nbytes) < max_length)
		input_event.ScheduleWrite();

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

		ssize_t nbytes = ReadToBuffer(output_fd, buffer, INT_MAX);
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

		switch (InvokeDirect(FdType::FD_PIPE, output_fd, NO_OFFSET,
				     INT_MAX, false)) {
		case IstreamDirectResult::BLOCKING:
		case IstreamDirectResult::CLOSED:
			break;

		case IstreamDirectResult::OK:
			output_event.ScheduleRead();
			break;

		case IstreamDirectResult::END:
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
	if (output_fd.IsDefined())
		Cancel();

	Destroy();
}

/*
 * child callback
 *
 */

void
SpawnIstream::OnChildProcessExit([[maybe_unused]] int status) noexcept
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
	UniqueFileDescriptor stdin_r, stdin_pipe;

	if (input) {
		std::tie(stdin_r, stdin_pipe) = CreatePipe();

		prepared.stdin_fd = stdin_r;

		stdin_pipe.SetNonBlocking();
	}

	auto [stdout_pipe, stdout_w] = CreatePipe();

	prepared.stdout_fd = stdout_w;

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

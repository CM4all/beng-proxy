/*
 * Copyright 2007-2018 Content Management AG
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

#include "SocketFilter.hxx"
#include "thread_job.hxx"
#include "event/DeferEvent.hxx"
#include "event/TimerEvent.hxx"
#include "SliceFifoBuffer.hxx"

#include <mutex>

class FilteredSocket;
struct ThreadSocketFilterInternal;
class ThreadQueue;

class ThreadSocketFilterHandler {
public:
	virtual ~ThreadSocketFilterHandler() {}

	/**
	 * Called in the main thread before Run() is scheduled.
	 */
	virtual void PreRun(ThreadSocketFilterInternal &) noexcept {}

	/**
	 * Do the work.  This is run in an unspecified worker thread.  The
	 * given #ThreadSocketFilter's mutex may be used for protection.
	 *
	 * This method may throw exceptions, which will be forwarded to
	 * BufferedSocketHandler::error().
	 */
	virtual void Run(ThreadSocketFilterInternal &f) = 0;

	/**
	 * Called in the main thread after one or more run() calls have
	 * finished successfully.
	 */
	virtual void PostRun(ThreadSocketFilterInternal &) noexcept {}
};

struct ThreadSocketFilterInternal : ThreadJob {
	/**
	 * True when #ThreadSocketFilterHandler's internal output buffers
	 * are empty.  Set by #ThreadSocketFilterHandler::run() before
	 * returning.
	 *
	 * Protected by #mutex.
	 */
	bool drained = true;

	/**
	 * True when no more input can be decrypted by
	 * #ThreadSocketFilterHandler.  Will be set to true by
	 * #ThreadSocketFilterHandler.
	 *
	 * Protected by #mutex.
	 */
	bool input_eof = false;

	/**
	 * Schedule the job again?  This can be used to fix up things that
	 * can only be done in the main thread.
	 *
	 * Protected by #mutex.
	 */
	bool again = false;

	/**
	 * True during the initial handshake.  Will be set to false by the
	 * #ThreadSocketFilterHandler.  It is used to control the
	 * #handshake_timeout_event.
	 *
	 * Protected by #mutex.
	 *
	 * TODO: this is only a kludge for the stable branch.  Reimplement
	 * properly.
	 */
	bool handshaking = true;

	mutable std::mutex mutex;

	/**
	 * A buffer of input data that was not yet handled by the filter.
	 * It will be passed to the filter, and after that, it will go to
	 * #decrypted_input.
	 *
	 * This gets fed from buffered_socket::input.  We need another
	 * buffer because buffered_socket is not thread-safe, while this
	 * buffer is protected by the #mutex.
	 */
	SliceFifoBuffer encrypted_input;

	/**
	 * A buffer of input data that was handled by the filter.  It will
	 * be passed to the handler.
	 */
	SliceFifoBuffer decrypted_input;

	/**
	 * A buffer of output data that was not yet handled by the filter.
	 * Once it was filtered, it will be written to #encrypted_output.
	 */
	SliceFifoBuffer plain_output;

	/**
	 * A buffer of output data that has been filtered already, and
	 * will be written to the socket.
	 */
	SliceFifoBuffer encrypted_output;
};

/**
 * A module for #filtered_socket that moves the filter to a thread
 * pool (see #thread_job).
 */
class ThreadSocketFilter final : public SocketFilter, ThreadSocketFilterInternal {
	ThreadQueue &queue;

	FilteredSocket *socket;

	/**
	 * The actual filter.  If this is NULL, then this object behaves
	 * just like #BufferedSocket.
	 */
	ThreadSocketFilterHandler *const handler;

	BoundMethod<void() noexcept> handshake_callback{nullptr};

	/**
	 * This event moves a call out of the current stack frame.  It is
	 * used by ScheduleWrite() to avoid calling InvokeWrite()
	 * directly.
	 */
	DeferEvent defer_event;

	/**
	 *
	 */
	TimerEvent handshake_timeout_event;

	bool busy = false, done_pending = false;

	bool connected = true;

	/**
	 * Does the handler expect more data?  It announced this by
	 * returning BUFFERED_MORE.
	 */
	bool expect_more = false;

	bool postponed_remaining = false;

	bool postponed_end = false;

	/**
	 * Set to true when the thread queue hasn't yet released the
	 * #thread_job.  The object will be destroyed in the "done"
	 * callback.
	 */
	bool postponed_destroy = false;

	/**
	 * True when the client has called
	 * filtered_socket_schedule_read().
	 */
	bool want_read = false;

	/**
	 * Was _schedule_read() forwarded?
	 */
	bool read_scheduled = false;

	/**
	 * True when the client has called
	 * filtered_socket_schedule_write().
	 */
	bool want_write = false;

	/**
	 * Data from ThreadSocketFilterInternal::decrypted_input gets
	 * moved here to be submitted.  This buffer is not protected by
	 * the mutex.
	 */
	SliceFifoBuffer unprotected_decrypted_input;

	Event::Duration read_timeout = Event::Duration(-1);

	/**
	 * If this is set, an exception was caught inside the thread, and
	 * shall be forwarded to the main thread.
	 */
	std::exception_ptr error;

public:
	ThreadSocketFilter(EventLoop &_event_loop,
			   ThreadQueue &queue,
			   ThreadSocketFilterHandler *handler) noexcept;

	ThreadSocketFilter(const ThreadSocketFilter &) = delete;

	~ThreadSocketFilter() noexcept;

	const ThreadSocketFilterHandler *GetHandler() const noexcept {
		return handler;
	}

private:
	/**
	 * Schedule a Run() call in a worker thread.
	 */
	void Schedule() noexcept;

	/**
	 * @return true if ThreadSocketFilterInternal::decrypted_input was
	 * full.
	 */
	bool MoveDecryptedInput() noexcept;

	/**
	 * Move data from ThreadSocketFilterInternal::decrypted_input to
	 * #unprotected_decrypted_input.
	 */
	void MoveDecryptedInputAndSchedule() noexcept;

	/**
	 * @return false if the object has been destroyed
	 */
	bool SubmitDecryptedInput() noexcept;

	/**
	 * @return the number of bytes appended to #plain_output
	 */
	size_t LockWritePlainOutput(const void *data, size_t size) noexcept;

	void ClosedPrematurely() noexcept;

	bool CheckRead(std::unique_lock<std::mutex> &lock) noexcept;
	bool CheckWrite(std::unique_lock<std::mutex> &lock) noexcept;

	void HandshakeTimeoutCallback() noexcept;

	/**
	 * Called in the main thread before scheduling a Run() call in a
	 * worker thread.
	 */
	void PreRun() noexcept;

	/**
	 * Called in the main thread after one or more Run() calls have
	 * finished successfully.
	 */
	void PostRun() noexcept;

	/**
	 * This event moves a call out of the current stack frame.  It is
	 * used by ScheduleWrite() to avoid calling InvokeWrite()
	 * directly.
	 */
	void OnDeferred() noexcept;

	/* virtual methods from class ThreadJob */
	void Run() noexcept final;
	void Done() noexcept final;

public:
	/* virtual methods from SocketFilter */
	void Init(FilteredSocket &_socket) noexcept override {
		socket = &_socket;
		Schedule();
	}

	void SetHandshakeCallback(BoundMethod<void() noexcept> callback) noexcept override;
	BufferedResult OnData() noexcept override;
	bool IsEmpty() const noexcept override;
	bool IsFull() const noexcept override;
	size_t GetAvailable() const noexcept override;
	WritableBuffer<void> ReadBuffer() noexcept override;
	void Consumed(size_t nbytes) noexcept override;
	bool Read(bool expect_more) noexcept override;
	ssize_t Write(const void *data, size_t length) noexcept override;
	void ScheduleRead(bool expect_more,
			  Event::Duration timeout) noexcept override;
	void ScheduleWrite() noexcept override;
	void UnscheduleWrite() noexcept override;
	bool InternalWrite() noexcept override;
	void OnClosed() noexcept override;
	bool OnRemaining(size_t remaining) noexcept override;
	void OnEnd() noexcept override;
	void Close() noexcept override;
};

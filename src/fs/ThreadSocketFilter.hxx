// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "SocketFilter.hxx"
#include "thread/Job.hxx"
#include "event/DeferEvent.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "memory/SliceFifoBuffer.hxx"

#include <exception> // for std::exception_ptr
#include <memory>
#include <mutex>

class FilteredSocket;
struct ThreadSocketFilterInternal;
class ThreadQueue;

class ThreadSocketFilterHandler {
	using ScheduleRunFunction = BoundMethod<void() noexcept>;
	ScheduleRunFunction schedule_run{nullptr};

public:
	virtual ~ThreadSocketFilterHandler() noexcept = default;

	void SetScheduleRunFunction(ScheduleRunFunction f) noexcept {
		schedule_run = f;
	}

protected:
	/**
	 * Schedule a Run() call.
	 *
	 * This method may only be called from the main thread.
	 */
	void ScheduleRun() noexcept {
		schedule_run();
	}

public:
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
	 * Called in the main thread after one or more Run() calls have
	 * finished successfully.
	 */
	virtual void PostRun(ThreadSocketFilterInternal &) noexcept {}

	/**
	 * Called in the main thread while the worker thread runs
	 * Run() and is unable to cancel it; this gives the
	 * #ThreadSocketFilterHandler a chance to fast-track
	 * cancellation.
	 *
	 * This cancellation may be permanent; it is only used while
	 * shutting down the connection.
	 */
	virtual void CancelRun(ThreadSocketFilterInternal &) noexcept {}
};

struct ThreadSocketFilterInternal : ThreadJob {
	/**
	 * True when #ThreadSocketFilterHandler's internal output buffers
	 * are empty.  Set by #ThreadSocketFilterHandler::Run() before
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

	/**
	 * True if Shutdown() should be handled by the
	 * #ThreadSocketFilterHandler.
	 */
	bool shutting_down = false;

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
 * A module for #FilteredSocket that moves the filter to a thread
 * pool (see #ThreadJob).
 */
class ThreadSocketFilter final : public SocketFilter, ThreadSocketFilterInternal {
	ThreadQueue &queue;

	FilteredSocket *socket;

	/**
	 * The actual filter.  If this is NULL, then this object behaves
	 * just like #BufferedSocket.
	 */
	const std::unique_ptr<ThreadSocketFilterHandler> handler;

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
	CoarseTimerEvent handshake_timeout_event;

	bool busy = false, done_pending = false;

	bool connected = true;

	bool postponed_remaining = false;

	bool postponed_end = false;

	/**
	 * Set to true when the thread queue hasn't yet released the
	 * #ThreadJob.  The object will be destroyed in the "done"
	 * callback.
	 */
	bool postponed_destroy = false;

	/**
	 * True when the client has called ScheduleRead().
	 */
	bool want_read = false;

	/**
	 * Was ScheduleRead() forwarded?
	 */
	bool read_scheduled = false;

	/**
	 * True when the client has called ScheduleWrite().
	 */
	bool want_write = false;

	/**
	 * Data from ThreadSocketFilterInternal::decrypted_input gets
	 * moved here to be submitted.  This buffer is not protected by
	 * the mutex.
	 */
	SliceFifoBuffer unprotected_decrypted_input;

	/**
	 * If this is set, an exception was caught inside the thread, and
	 * shall be forwarded to the main thread.
	 */
	std::exception_ptr error;

public:
	ThreadSocketFilter(ThreadQueue &queue,
			   std::unique_ptr<ThreadSocketFilterHandler> _handler) noexcept;

	ThreadSocketFilter(const ThreadSocketFilter &) = delete;

	~ThreadSocketFilter() noexcept;

	const ThreadSocketFilterHandler &GetHandler() const noexcept {
		return *handler;
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
	std::size_t LockWritePlainOutput(std::span<const std::byte> src) noexcept;

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
	std::size_t GetAvailable() const noexcept override;
	std::span<std::byte> ReadBuffer() noexcept override;
	void Consumed(std::size_t nbytes) noexcept override;
	void AfterConsumed() noexcept override;
	BufferedReadResult Read() noexcept override;
	ssize_t Write(std::span<const std::byte> src) noexcept override;
	void ScheduleRead() noexcept override;
	void ScheduleWrite() noexcept override;
	void UnscheduleWrite() noexcept override;
	bool InternalWrite() noexcept override;
	void Shutdown() noexcept override;
	void OnClosed() noexcept override;
	bool OnRemaining(std::size_t remaining) noexcept override;
	void OnEnd() override;
	void Close() noexcept override;
};

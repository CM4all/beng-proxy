// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ThreadSocketFilter.hxx"
#include "FilteredSocket.hxx"
#include "memory/fb_pool.hxx"
#include "thread/Queue.hxx"
#include "system/Error.hxx"
#include "net/SocketProtocolError.hxx"

#include <algorithm>

ThreadSocketFilter::ThreadSocketFilter(EventLoop &_event_loop,
				       ThreadQueue &_queue,
				       std::unique_ptr<ThreadSocketFilterHandler> _handler) noexcept
	:queue(_queue),
	 handler(std::move(_handler)),
	 defer_event(_event_loop, BIND_THIS_METHOD(OnDeferred)),
	 handshake_timeout_event(_event_loop,
				 BIND_THIS_METHOD(HandshakeTimeoutCallback))
{
	assert(handler);

	handshake_timeout_event.Schedule(std::chrono::minutes(1));

	handler->SetScheduleRunFunction(BIND_THIS_METHOD(Schedule));
}

ThreadSocketFilter::~ThreadSocketFilter() noexcept = default;

void
ThreadSocketFilter::ClosedPrematurely() noexcept
{
	socket->InvokeError(std::make_exception_ptr(SocketClosedPrematurelyError{}));
}

void
ThreadSocketFilter::Schedule() noexcept
{
	assert(!postponed_destroy);

	PreRun();

	queue.Add(*this);
}

void
ThreadSocketFilter::SetHandshakeCallback(BoundMethod<void() noexcept> callback) noexcept
{
	assert(!handshake_callback);
	assert(callback);

	const std::scoped_lock lock{mutex};
	if (handshaking)
		/* defer */
		handshake_callback = callback;
	else
		/* handshake is already complete */
		callback();
}

inline bool
ThreadSocketFilter::MoveDecryptedInput() noexcept
{
	assert(!unprotected_decrypted_input.IsDefinedAndFull());

	const std::scoped_lock lock{mutex};
	const bool was_full = decrypted_input.IsDefinedAndFull();
	unprotected_decrypted_input.MoveFromAllowBothNull(decrypted_input);
	unprotected_decrypted_input.FreeIfEmpty();
	return was_full;
}

void
ThreadSocketFilter::MoveDecryptedInputAndSchedule() noexcept
{
	if (MoveDecryptedInput())
		/* just in case the filter has stalled because the
		   decrypted_input buffer was full: try again */
		Schedule();
}

/**
 * @return false if the object has been destroyed
 */
bool
ThreadSocketFilter::SubmitDecryptedInput() noexcept
{
	if (unprotected_decrypted_input.empty())
		MoveDecryptedInputAndSchedule();

	while (true) {
		if (unprotected_decrypted_input.empty())
			return true;

		want_read = false;

		switch (socket->InvokeData()) {
		case BufferedResult::OK:
			AfterConsumed();
			return true;

		case BufferedResult::MORE:
			if (unprotected_decrypted_input.IsDefinedAndFull()) {
				socket->InvokeError(std::make_exception_ptr(SocketBufferFullError{}));
				return false;
			}

			{
				const std::size_t available =
					unprotected_decrypted_input.GetAvailable();
				AfterConsumed();
				if (unprotected_decrypted_input.GetAvailable() > available)
					/* more data has just arrived from the
					   worker thread; try again */
					continue;
			}

			return true;

		case BufferedResult::AGAIN:
			AfterConsumed();
			continue;

		case BufferedResult::CLOSED:
			return false;
		}
	}
}

inline bool
ThreadSocketFilter::CheckRead(std::unique_lock<std::mutex> &lock) noexcept
{
	if (!want_read || encrypted_input.IsDefinedAndFull() ||
	    !connected || read_scheduled)
		return true;

	read_scheduled = true;
	lock.unlock();
	socket->InternalScheduleRead();
	lock.lock();

	return true;
}

inline bool
ThreadSocketFilter::CheckWrite(std::unique_lock<std::mutex> &lock) noexcept
{
	if (!want_write || plain_output.IsDefinedAndFull())
		return true;

	lock.unlock();

	want_write = false;

	if (!socket->InvokeWrite())
		return false;

	lock.lock();
	return true;
}

void
ThreadSocketFilter::OnDeferred() noexcept
{
	std::unique_lock lock{mutex};

	if (!CheckRead(lock) || !CheckWrite(lock))
		return;
}

void
ThreadSocketFilter::HandshakeTimeoutCallback() noexcept
{
	bool _handshaking;

	{
		const std::scoped_lock lock{mutex};
		_handshaking = handshaking;
	}

	if (_handshaking)
		socket->InvokeTimeout();
}

void
ThreadSocketFilter::PreRun() noexcept
{
	{
		const std::scoped_lock lock{mutex};
		decrypted_input.AllocateIfNull(fb_pool_get());
		encrypted_output.AllocateIfNull(fb_pool_get());
	}

	handler->PreRun(*this);
}

void
ThreadSocketFilter::PostRun() noexcept
{
	handler->PostRun(*this);

	{
		const std::scoped_lock lock{mutex};
		decrypted_input.FreeIfEmpty();
		encrypted_output.FreeIfEmpty();
	}
}

/*
 * thread_job
 *
 */

void
ThreadSocketFilter::Run() noexcept
{
	{
		const std::scoped_lock lock{mutex};

		if (error != nullptr)
			return;

		busy = true;
	}

	std::exception_ptr new_error;

	try {
		handler->Run(*this);
	} catch (...) {
		new_error = std::current_exception();
	}

	{
		const std::scoped_lock lock{mutex};

		busy = false;
		done_pending = true;

		assert(!error);
		error = std::move(new_error);
	}
}

void
ThreadSocketFilter::Done() noexcept
{
	if (postponed_destroy) {
		/* the object has been closed, and now that the thread has
		   finished, we can finally destroy it */
		delete this;
		return;
	}

	std::unique_lock lock{mutex};

	done_pending = false;

	if (error != nullptr) {
		/* an error has occurred inside the worker thread: forward it
		   to the FilteredSocket */

		if (socket->IsConnected()) {
			/* flush the encrypted_output buffer, because it may
			   contain a "TLS alert" */
			auto r = encrypted_output.Read();
			if (!r.empty()) {
				/* don't care for the return value; the socket and
				   this object are going to be closed anyway */
				socket->InternalDirectWrite(r);
				socket->Shutdown();
			}
		}

		std::exception_ptr error2 = std::move(error);
		error = nullptr;

		lock.unlock();
		socket->InvokeError(error2);
		return;
	}

	if (connected && input_eof) {
		/* this condition was signalled by
		   ThreadSocketFilterHandler::Run(), probably because a TLS
		   "close notify" alert was received */

		encrypted_input.FreeIfDefined();

		input_eof = false;

		lock.unlock();

		/* first flush data which was already decrypted; that is
		   important because there will not be a socket event
		   triggering this */
		if (!SubmitDecryptedInput())
			return;

		/* now pretend the peer has closed the connection */
		if (!socket->ClosedByPeer())
			return;

		lock.lock();
	}

	if (postponed_end && encrypted_input.empty() && !again) {
		if (postponed_remaining) {
			if (!decrypted_input.empty() ||
			    !unprotected_decrypted_input.empty()) {
				/* before we actually deliver the "remaining" event,
				   we should give the handler a chance to process the
				   data */

				lock.unlock();

				if (!SubmitDecryptedInput())
					return;

				lock.lock();
			}

			const std::size_t available = decrypted_input.GetAvailable() +
				unprotected_decrypted_input.GetAvailable();
			lock.unlock();

			postponed_remaining = false;

			if (!socket->InvokeRemaining(available))
				return;

			lock.lock();
		}

		if (decrypted_input.empty() &&
		    unprotected_decrypted_input.empty()) {
			lock.unlock();

			socket->InvokeEnd();
			return;
		}

		lock.unlock();
		return;
	}

	if (connected) {
		// TODO: timeouts?

		if (!handshaking && handshake_callback) {
			auto callback = handshake_callback;
			handshake_callback = nullptr;
			callback();
		}

		if (!encrypted_input.IsDefinedAndFull())
			socket->InternalScheduleRead();

		if (!encrypted_output.empty())
			/* be optimistic and assume the socket is
			   already writable (calling
			   BufferedSocket::DeferWrite() instead of
			   BufferedSocket::ScheduleWrite()); this is
			   because TLS often needs to transmit small
			   packets */
			socket->InternalDeferWrite();
	}

	if (!CheckWrite(lock))
		return;

	const bool drained2 = connected && drained &&
		plain_output.empty() &&
		encrypted_output.empty();

	encrypted_input.FreeIfEmpty();
	plain_output.FreeIfEmpty();

	bool _again = again;
	again = false;

	lock.unlock();

	if (drained2 && !socket->InternalDrained())
		return;

	if (!SubmitDecryptedInput())
		return;

	if (_again)
		Schedule();
	else
		PostRun();
}

/*
 * socket_filter
 *
 */

BufferedResult
ThreadSocketFilter::OnData() noexcept
{
	read_scheduled = false;

	{
		const std::scoped_lock lock{mutex};

		if (encrypted_input.IsDefinedAndFull())
			return BufferedResult::OK;

		auto &src = socket->InternalGetInputBuffer();
		assert(!src.empty());

		encrypted_input.MoveFromAllowBothNull(src);
		src.FreeIfEmpty();
	}

	Schedule();

	return BufferedResult::OK;
}

bool
ThreadSocketFilter::IsEmpty() const noexcept
{
	const std::scoped_lock lock{mutex};
	return decrypted_input.empty() &&
		unprotected_decrypted_input.empty();
}

bool
ThreadSocketFilter::IsFull() const noexcept
{
	const std::scoped_lock lock{mutex};
	return decrypted_input.IsDefinedAndFull() &&
		unprotected_decrypted_input.IsDefinedAndFull();
}

std::size_t
ThreadSocketFilter::GetAvailable() const noexcept
{
	const std::scoped_lock lock{mutex};
	return decrypted_input.GetAvailable() +
		unprotected_decrypted_input.GetAvailable();
}

std::span<std::byte>
ThreadSocketFilter::ReadBuffer() noexcept
{
	return unprotected_decrypted_input.Read();
}

void
ThreadSocketFilter::Consumed(std::size_t nbytes) noexcept
{
	if (nbytes == 0)
		return;

	assert(unprotected_decrypted_input.IsDefined());

	unprotected_decrypted_input.Consume(nbytes);
	unprotected_decrypted_input.FreeIfEmpty();
}

void
ThreadSocketFilter::AfterConsumed() noexcept
{
	if (!unprotected_decrypted_input.IsDefinedAndFull())
		MoveDecryptedInputAndSchedule();
}

bool
ThreadSocketFilter::Read() noexcept
{
	return SubmitDecryptedInput() &&
		(postponed_end ||
		 socket->InternalRead());
}

inline std::size_t
ThreadSocketFilter::LockWritePlainOutput(std::span<const std::byte> src) noexcept
{
	const std::scoped_lock lock{mutex};

	plain_output.AllocateIfNull(fb_pool_get());
	return plain_output.MoveFrom(src);
}

ssize_t
ThreadSocketFilter::Write(std::span<const std::byte> src) noexcept
{
	// TODO: is this check necessary?
	if (src.empty())
		return 0;

	const std::size_t nbytes = LockWritePlainOutput(src);

	if (nbytes < src.size())
		/* set the "want_write" flag but don't schedule an event to
		   avoid a busy loop; as soon as the worker thread returns, we
		   will retry to write according to this flag */
		want_write = true;

	if (nbytes == 0)
		return WRITE_BLOCKING;

	socket->InternalUndrained();
	Schedule();

	return nbytes;
}

void
ThreadSocketFilter::ScheduleRead() noexcept
{
	want_read = true;
	read_scheduled = false;

	defer_event.Schedule();
}

void
ThreadSocketFilter::ScheduleWrite() noexcept
{
	if (want_write)
		return;

	want_write = true;
	defer_event.Schedule();
}

void
ThreadSocketFilter::UnscheduleWrite() noexcept
{
	if (!want_write)
		return;

	want_write = false;

	if (!want_read)
		defer_event.Cancel();
}

bool
ThreadSocketFilter::InternalWrite() noexcept
{
	std::unique_lock lock{mutex};

	auto r = encrypted_output.Read();
	if (r.empty()) {
		lock.unlock();
		socket->InternalUnscheduleWrite();
		return true;
	}

	/* copy to stack, unlock */
	assert(r.size() <= FB_SIZE);
	std::byte copy[FB_SIZE];
	std::copy(r.begin(), r.end(), copy);
	lock.unlock();

	ssize_t nbytes = socket->InternalWrite(std::span{copy}.first(r.size()));
	if (nbytes > 0) {
		lock.lock();
		const bool add = encrypted_output.IsFull();
		encrypted_output.Consume(nbytes);
		encrypted_output.FreeIfEmpty();
		const bool empty = encrypted_output.empty();
		const bool _drained = empty && drained && plain_output.empty();
		lock.unlock();

		if (add)
			/* the filter job may be stalled because the output buffer
			   was full; try again, now that it's not full anymore */
			Schedule();

		if (empty)
			socket->InternalUnscheduleWrite();
		else if (std::size_t(nbytes) < r.size())
			/* if this was only a partial write, and this
			   InternalWrite() was triggered by
			   BufferedSocket::DeferWrite() (which is
			   one-shot), we need to register EPOLLOUT to
			   trigger further writes */
			socket->InternalScheduleWrite();

		if (_drained && !socket->InternalDrained())
			return false;

		return true;
	} else {
		switch ((enum write_result)nbytes) {
		case WRITE_SOURCE_EOF:
			assert(false);
			gcc_unreachable();

		case WRITE_ERRNO:
			socket->InvokeError(std::make_exception_ptr(MakeErrno("write error")));
			return false;

		case WRITE_BLOCKING:
			return true;

		case WRITE_DESTROYED:
			return false;

		case WRITE_BROKEN:
			return true;
		}

		assert(false);
		gcc_unreachable();
	}
}

void
ThreadSocketFilter::OnClosed() noexcept
{
	assert(connected);
	assert(!postponed_remaining);

	connected = false;
	want_write = false;

	handshake_timeout_event.Cancel();
}

bool
ThreadSocketFilter::OnRemaining(std::size_t remaining) noexcept
{
	assert(!connected);
	assert(!want_write);
	assert(!postponed_remaining);

	if (remaining == 0) {
		std::unique_lock lock{mutex};

		if (!busy && !again && !done_pending && encrypted_input.empty()) {
			const std::size_t available = decrypted_input.GetAvailable() +
				unprotected_decrypted_input.GetAvailable();
			lock.unlock();

			/* forward the call */
			return socket->InvokeRemaining(available);
		}
	}

	/* there's still encrypted input - postpone the remaining() call
	   until we have decrypted everything */

	postponed_remaining = true;
	return true;
}

void
ThreadSocketFilter::OnEnd() noexcept
{
	assert(!postponed_end);

	if (postponed_remaining) {
		/* see if we can commit the "remaining" call now */
		std::unique_lock lock{mutex};

		if (!busy && !again && !done_pending && encrypted_input.empty()) {
			const std::size_t available = decrypted_input.GetAvailable() +
				unprotected_decrypted_input.GetAvailable();
			lock.unlock();

			postponed_remaining = false;
			if (!socket->InvokeRemaining(available))
				return;
		} else {
			/* postpone both "remaining" and "end" */
			postponed_end = true;
			return;
		}
	}

	/* forward the "end" call as soon as the decrypted_input buffer
	   becomes empty */

	bool empty;
	{
		const std::scoped_lock lock{mutex};
		assert(encrypted_input.empty());
		empty = decrypted_input.empty() &&
			unprotected_decrypted_input.empty();
	}

	if (empty)
		/* already empty: forward the call now */
		socket->InvokeEnd();
	else
		/* postpone */
		postponed_end = true;
}

void
ThreadSocketFilter::Close() noexcept
{
	defer_event.Cancel();

	if (!queue.Cancel(*this)) {
		/* postpone the destruction */
		postponed_destroy = true;

		handler->CancelRun(*this);
		return;
	}

	delete this;
}

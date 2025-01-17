// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "io/uring/config.h" // for HAVE_URING

#ifdef HAVE_URING
#include "Internal.hxx"
#include "memory/GrowingBuffer.hxx"
#include "fs/FilteredSocket.hxx"
#include "system/Error.hxx"
#include "io/uring/Queue.hxx"

#include <cassert>

#include <fcntl.h> // for SPLICE_F_MOVE

class HttpServerConnection::UringSend final : Uring::Operation {
	HttpServerConnection &parent;
	Uring::Queue &queue;

	GrowingBufferReader reader;

	bool canceled = false;

public:
	UringSend(HttpServerConnection &_parent,
		  Uring::Queue &_queue,
		  GrowingBuffer &&buffer) noexcept
		:parent(_parent), queue(_queue), reader(std::move(buffer))
	{
	}

	void Start();

	void Cancel() noexcept;

private:
	HttpServerConnection &Finish() noexcept {
		assert(!IsUringPending());
		assert(!canceled);
		assert(parent.uring_send == this);

		auto &_parent = parent;
		parent.uring_send = nullptr;
		delete this;
		return _parent;
	}

	void OnUringCompletion(int res) noexcept override;
};

void
HttpServerConnection::UringSend::Start()
{
	assert(!IsUringPending());
	assert(!canceled);
	assert(parent.uring_send == this);

	const auto r = reader.Read();
	if (r.empty()) {
		Finish().OnUringSendDone();
		return;
	}

	auto &s = queue.RequireSubmitEntry();
	io_uring_prep_send(&s, parent.socket->GetSocket().Get(),
			   r.data(), r.size(), 0);

	/* always go async; this way, the overhead for the operation
           does not cause latency in the main thread */
	io_uring_sqe_set_flags(&s, IOSQE_ASYNC);

	queue.Push(s, *this);
}

void
HttpServerConnection::UringSend::Cancel() noexcept
{
	assert(IsUringPending());
	assert(!canceled);
	assert(parent.uring_send == this);

	canceled = true;
	parent.uring_send = nullptr;

	auto &s = queue.RequireSubmitEntry();
	io_uring_prep_cancel(&s, GetUringData(), 0);
	io_uring_sqe_set_data(&s, nullptr);
	queue.Submit();
}

void
HttpServerConnection::UringSend::OnUringCompletion(int res) noexcept
{
	if (canceled) [[unlikely]] {
		delete this;
		return;
	}

	assert(parent.uring_send == this);

	if (res < 0) [[unlikely]] {
		Finish().OnUringSendError(-res);
		return;
	}

	reader.Consume(res);
	Start();
}

void
HttpServerConnection::StartUringSend(Uring::Queue &queue, GrowingBuffer &&src)
{
	assert(uring_send == nullptr);

	uring_send = new UringSend(*this, queue, std::move(src));
	uring_send->Start();
}

void
HttpServerConnection::CancelUringSend() noexcept
{
	if (uring_send != nullptr) {
		uring_send->Cancel();
		assert(uring_send == nullptr);
	}
}

inline void
HttpServerConnection::OnUringSendDone() noexcept
{
	if (HasInput())
		DeferWrite();
	else
		ResponseIstreamFinished();
}

inline void
HttpServerConnection::OnUringSendError(int error) noexcept
{
	Error(std::make_exception_ptr(MakeErrno(error, "Send failed")));
}

HttpServerConnection::UringSplice::~UringSplice() noexcept
{
	if (IsUringPending()) {
		if (auto *s = queue.GetSubmitEntry()) {
			io_uring_prep_cancel(s, GetUringData(), 0);
			io_uring_sqe_set_data(s, nullptr);
			queue.Submit();
		}

		CancelUring();
	}
}

void
HttpServerConnection::UringSplice::Start(FileDescriptor src, off_t offset,
					 std::size_t _max_length, bool _then_eof)
{
	assert(!IsUringPending());

	max_length = _max_length;
	then_eof = _then_eof;

	auto &s = queue.RequireSubmitEntry();
	io_uring_prep_splice(&s, src.Get(), offset,
			     parent.socket->GetSocket().Get(), -1,
			     max_length, SPLICE_F_MOVE);
	queue.Push(s, *this);
}

inline void
HttpServerConnection::OnUringSpliceCompletion(int nbytes,
					      std::size_t max_length, bool then_eof) noexcept
{
	if (nbytes <= 0) [[unlikely]] {
		if (nbytes == 0)
			Error("Pipe ended prematurely");
		else if (nbytes != -EAGAIN)
			Error(std::make_exception_ptr(MakeErrno(-nbytes, "Splice failed")));
		return;
	}

	input.ConsumeDirect(nbytes);
	response.bytes_sent += nbytes;
	response.length += (off_t)nbytes;

	if (then_eof && static_cast<std::size_t>(nbytes) == max_length) {
		CloseInput();
		ResponseIstreamFinished();
		return;
	}

	ScheduleWrite();
}

void
HttpServerConnection::UringSplice::OnUringCompletion(int res) noexcept
{
	parent.OnUringSpliceCompletion(res, max_length, then_eof);
}

#endif

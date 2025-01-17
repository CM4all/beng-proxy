// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "io/uring/config.h" // for HAVE_URING

#ifdef HAVE_URING
#include "Internal.hxx"
#include "fs/FilteredSocket.hxx"
#include "system/Error.hxx"
#include "io/uring/Queue.hxx"

#include <fcntl.h> // for SPLICE_F_MOVE

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

/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "UringIstream.hxx"
#include "istream.hxx"
#include "New.hxx"
#include "io/Iovec.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/uring/Operation.hxx"
#include "io/uring/Queue.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "system/Error.hxx"
#include "util/RuntimeError.hxx"

#include <memory>

#include <assert.h>
#include <limits.h>

class CanceledUringIstream final : public Uring::Operation {
	std::unique_ptr<struct iovec> iov;

	SliceFifoBuffer buffer;

public:
	CanceledUringIstream(std::unique_ptr<struct iovec> &&_iov,
			     SliceFifoBuffer &&_buffer) noexcept
		:iov(std::move(_iov)), buffer(std::move(_buffer)) {}

	void OnUringCompletion(int) noexcept override {
		/* ignore the result and delete this object, which
		   will free the buffer */
		delete this;
	}
};

class UringIstream final : public Istream, Uring::Operation {
	Uring::Queue &uring;

	UniqueFileDescriptor fd;

	/**
	 * Passed to the io_uring read operation.
	 *
	 * It is allocated on the heap, because the kernel may access
	 * it if a read is still in flight and this object is
	 * destroyed; ownership will be passed to
	 * #CanceledUringIstream.
	 */
	std::unique_ptr<struct iovec> iov = std::make_unique<struct iovec>();

	/**
	 * The file offset of the next/pending read operation.  If
	 * there is data in the #buffer, it precedes this offset.
	 */
	off_t offset;

	/**
	 * We'll stop reading at this file offset.  This is usually
	 * the file size (or the end of the requested range).
	 */
	const off_t end_offset;

	SliceFifoBuffer buffer;

	/**
	 * The path name.  Only used for error messages.
	 */
	const char *const path;

public:
	UringIstream(struct pool &p, Uring::Queue &_uring,
		     const char *_path, UniqueFileDescriptor &&_fd,
		     off_t _start_offset, off_t _end_offset) noexcept
		:Istream(p), uring(_uring),
		 fd(std::move(_fd)),
		 offset(_start_offset), end_offset(_end_offset),
		 path(_path)
	{
	}

	~UringIstream() noexcept;

private:
	gcc_pure
	size_t GetMaxRead() const noexcept {
		return std::min(end_offset - offset, off_t(INT_MAX));
	}

	void StartRead() noexcept;

	/* virtual methods from class Uring::Operation */
	void OnUringCompletion(int res) noexcept override;

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override;
	off_t _Skip(gcc_unused off_t length) noexcept override;

	void _Read() noexcept override {
		// TODO free the buffer?
		if (ConsumeFromBuffer(buffer) == 0 && !IsUringPending())
			StartRead();
		// TODO "direct"?
	}

	// TODO: _FillBucketList, _ConsumeBucketList

	int _AsFd() noexcept override;
	void _Close() noexcept override {
		Destroy();
	}
};

UringIstream::~UringIstream() noexcept
{
	if (IsUringPending()) {
		/* the operation is still pending, and we must not
		   release the buffer yet, or the kernel will later
		   write into this buffer which then belongs somebody
		   else */

		assert(buffer.IsDefined());

		auto *c = new CanceledUringIstream(std::move(iov),
						   std::move(buffer));
		ReplaceUring(*c);
	}
}

void
UringIstream::StartRead() noexcept
{
	assert(!IsUringPending());
	assert(!buffer.IsDefinedAndFull());

	size_t max_read = GetMaxRead();
	if (max_read == 0) {
		if (buffer.empty())
			DestroyEof();

		return;
	}

	if (buffer.IsNull())
		buffer.Allocate(fb_pool_get());

	auto &s = uring.RequireSubmitEntry();

	ConstBuffer<uint8_t> w = buffer.Write();
	assert(!w.empty());
	if (w.size > GetMaxRead())
		w.size = GetMaxRead();

	*iov = MakeIovec(w);
	io_uring_prep_readv(&s, fd.Get(), iov.get(), 1, offset);

	uring.Push(s, *this);
}

void
UringIstream::OnUringCompletion(int res) noexcept
try {
	if (res < 0)
		throw FormatErrno(-res, "Failed to read from %s", path);

	if (res == 0)
		throw FormatRuntimeError("Premature end of file in '%s'", path);

	buffer.Append(res);
	offset += res;

	// TODO free the buffer?
	if (SendFromBuffer(buffer) > 0)
		StartRead();
} catch (...) {
	DestroyError(std::current_exception());
}

off_t
UringIstream::_GetAvailable(bool) noexcept
{
	return (end_offset - offset) + buffer.GetAvailable();
}

off_t
UringIstream::_Skip(off_t length) noexcept
{
	if (length == 0)
		return 0;

	const size_t buffer_available = buffer.GetAvailable();
	if (length <= off_t(buffer_available)) {
		buffer.Consume(length);
		Consumed(length);
		return length;
	}

	buffer.Consume(buffer_available);
	Consumed(buffer_available);

	// TODO: skip more data?  what about the pending read?

	return buffer_available;
}

int
UringIstream::_AsFd() noexcept
{
	int result_fd = fd.Steal();

	Destroy();

	return result_fd;
}

/*
 * constructor and public methods
 *
 */

UnusedIstreamPtr
NewUringIstream(Uring::Queue &uring, struct pool &pool,
		const char *path, UniqueFileDescriptor fd,
		off_t start_offset, off_t end_offset) noexcept
{
	assert(fd.IsDefined());
	assert(start_offset <= end_offset);

	return NewIstreamPtr<UringIstream>(pool, uring, path, std::move(fd),
					   start_offset, end_offset);
}

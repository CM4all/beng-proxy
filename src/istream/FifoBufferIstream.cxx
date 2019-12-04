/*
 * Copyright 2007-2019 Content Management AG
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

#include "FifoBufferIstream.hxx"
#include "istream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "SliceFifoBuffer.hxx"
#include "fb_pool.hxx"

#include <string.h>

class FifoBufferIstream final : public Istream {
	FifoBufferIstreamHandler &handler;

	const SharedPoolPtr<FifoBufferIstreamControl> control;

	SliceFifoBuffer buffer;

	bool eof = false;

public:
	FifoBufferIstream(struct pool &p,
			  FifoBufferIstreamHandler &_handler) noexcept
		:Istream(p),
		 handler(_handler),
		 control(SharedPoolPtr<FifoBufferIstreamControl>::Make(p, *this)) {}

	~FifoBufferIstream() noexcept {
		control->fbi = nullptr;
	}

	auto GetControl() noexcept {
		return control;
	}

	auto &GetBuffer() noexcept {
		return buffer;
	}

	size_t Push(ConstBuffer<void> src) noexcept;

	void SetEof() noexcept;

	using Istream::DestroyError;

	void SubmitBuffer() noexcept;

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override {
		return partial || eof
			? (off_t)buffer.GetAvailable()
			: (off_t)-1;
	}

	off_t _Skip(off_t length) noexcept override;
	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) noexcept override;
	size_t _ConsumeBucketList(size_t nbytes) noexcept override;

	void _Close() noexcept override {
		if (!eof)
			handler.OnFifoBufferIstreamClosed();
		Istream::_Close();
	}
};

SliceFifoBuffer *
FifoBufferIstreamControl::GetBuffer() noexcept
{
	return fbi != nullptr
		? &fbi->GetBuffer()
		: nullptr;
}

void
FifoBufferIstreamControl::SubmitBuffer() noexcept
{
	if (fbi != nullptr)
		fbi->SubmitBuffer();
}

void
FifoBufferIstreamControl::SetEof() noexcept
{
	if (fbi != nullptr)
		fbi->SetEof();
}

void
FifoBufferIstreamControl::DestroyError(std::exception_ptr e) noexcept
{
	if (fbi != nullptr)
		fbi->DestroyError(std::move(e));
}

size_t
FifoBufferIstream::Push(ConstBuffer<void> src) noexcept
{
	buffer.AllocateIfNull(fb_pool_get());

	auto w = buffer.Write();
	size_t nbytes = std::min(w.size, src.size);
	memcpy(w.data, src.data, nbytes);
	buffer.Append(nbytes);
	return nbytes;
}

void
FifoBufferIstream::SetEof() noexcept
{
	eof = true;

	if (buffer.empty())
		DestroyEof();
}

void
FifoBufferIstream::SubmitBuffer() noexcept
{
	while (!buffer.empty()) {
		if (SendFromBuffer(buffer) == 0)
			return;

		if (buffer.empty() && !eof)
			handler.OnFifoBufferIstreamDrained();
	}

	if (buffer.empty()) {
		if (eof)
			DestroyEof();
		else
			buffer.FreeIfDefined();
	}
}

off_t
FifoBufferIstream::_Skip(off_t length) noexcept
{
	size_t nbytes = std::min<decltype(length)>(length, buffer.GetAvailable());
	buffer.Consume(nbytes);
	buffer.FreeIfEmpty();
	return nbytes;
}

void
FifoBufferIstream::_Read() noexcept
{
	SubmitBuffer();
}

void
FifoBufferIstream::_FillBucketList(IstreamBucketList &list) noexcept
{
	auto r = buffer.Read();
	if (!r.empty())
		list.Push(r.ToVoid());

	if (!eof)
		list.SetMore();
}

size_t
FifoBufferIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	size_t consumed = std::min(nbytes, buffer.GetAvailable());
	buffer.Consume(consumed);

	if (consumed > 0 && buffer.empty() && !eof) {
		handler.OnFifoBufferIstreamDrained();

		if (buffer.empty())
			buffer.Free();
	}

	return nbytes - consumed;
}

std::pair<UnusedIstreamPtr, SharedPoolPtr<FifoBufferIstreamControl>>
NewFifoBufferIstream(struct pool &pool,
		     FifoBufferIstreamHandler &handler) noexcept
{
	auto *i = NewIstream<FifoBufferIstream>(pool, handler);
	return std::make_pair(UnusedIstreamPtr(i), i->GetControl());
}

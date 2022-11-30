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

#include "ApproveIstream.hxx"
#include "ForwardIstream.hxx"
#include "Bucket.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "event/DeferEvent.hxx"
#include "io/FileDescriptor.hxx"

class ApproveIstream final : public ForwardIstream {
	const SharedPoolPtr<ApproveIstreamControl> control;

	DeferEvent defer_read;

	off_t approved = 0;

public:
	ApproveIstream(struct pool &p, EventLoop &event_loop,
		       UnusedIstreamPtr _input) noexcept
		:ForwardIstream(p, std::move(_input)),
		 control(SharedPoolPtr<ApproveIstreamControl>::Make(p, *this)),
		 defer_read(event_loop, BIND_THIS_METHOD(DeferredRead)) {}

	~ApproveIstream() noexcept override {
		control->approve = nullptr;
	}

	auto GetControl() noexcept {
		return control;
	}

	void Approve(off_t nbytes) noexcept {
		if (approved <= 0)
			defer_read.Schedule();

		approved += nbytes;
	}

private:
	void DeferredRead() noexcept {
		ForwardIstream::_Read();
	}

protected:
	/* virtual methods from class Istream */

	off_t _Skip(off_t length) noexcept override {
		if (approved <= 0)
			return -1;

		if (length > approved)
			length = approved;

		return ForwardIstream::_Skip(length);
	}

	void _Read() noexcept override {
		if (approved > 0)
			ForwardIstream::_Read();
	}

	void _FillBucketList(IstreamBucketList &list) override {
		if (approved <= 0) {
			list.SetMore();
			return;
		}

		IstreamBucketList tmp;

		try {
			input.FillBucketList(tmp);
		} catch (...) {
			Destroy();
			throw;
		}

		list.SpliceBuffersFrom(std::move(tmp), approved);
	}

	int _AsFd() noexcept override {
		return -1;
	}

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		if (approved <= 0)
			return 0;

		if ((off_t)src.size() > approved)
			src = src.first((std::size_t)approved);

		return ForwardIstream::OnData(src);
	}

	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset,
				     std::size_t max_length) noexcept override {
		if (approved <= 0)
			return IstreamDirectResult::BLOCKING;

		if ((off_t)max_length > approved)
			max_length = (std::size_t)approved;

		return ForwardIstream::OnDirect(type, fd, offset, max_length);
	}
};

void
ApproveIstreamControl::Approve(off_t nbytes) noexcept
{
	if (approve != nullptr)
		approve->Approve(nbytes);
}

std::pair<UnusedIstreamPtr, SharedPoolPtr<ApproveIstreamControl>>
NewApproveIstream(struct pool &pool, EventLoop &event_loop,
		  UnusedIstreamPtr input)
{
	auto *i = NewIstream<ApproveIstream>(pool, event_loop, std::move(input));
	return std::make_pair(UnusedIstreamPtr{i}, i->GetControl());
}

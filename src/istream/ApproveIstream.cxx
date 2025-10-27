// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ApproveIstream.hxx"
#include "ForwardIstream.hxx"
#include "Bucket.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "event/DeferEvent.hxx"
#include "io/FileDescriptor.hxx"

#include <cassert>

class ApproveIstream final : public ForwardIstream {
	const SharedPoolPtr<ApproveIstreamControl> control;

	DeferEvent defer_read;

	uint_least64_t approved = 0;

public:
	ApproveIstream(struct pool &p, EventLoop &event_loop,
		       UnusedIstreamPtr _input) noexcept
		:ForwardIstream(p, std::move(_input)),
		 control(SharedPoolPtr<ApproveIstreamControl>::Make(p, *this)),
		 defer_read(event_loop, BIND_THIS_METHOD(DeferredRead)) {}

	~ApproveIstream() noexcept override {
		assert(control);
		assert(control->approve == this);

		control->approve = nullptr;
	}

	auto GetControl() noexcept {
		return control;
	}

	void Approve(uint_least64_t nbytes) noexcept {
		if (approved == 0)
			defer_read.Schedule();

		approved += nbytes;
	}

private:
	void DeferredRead() noexcept {
		Read();
	}

protected:
	/* virtual methods from class Istream */

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
		ForwardIstream::_FillBucketList(tmp);

		const std::size_t nbytes = list.SpliceBuffersFrom(std::move(tmp), approved);
		if (nbytes >= approved) {
			if (nbytes > approved)
				/* there was more data in "tmp" than what was
				   approved */
				list.SetMore();
			else
				/* our input may have more data
				   eventually */
				list.CopyMoreFlagsFrom(tmp);
		}
	}

	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override {
		auto result = ForwardIstream::_ConsumeBucketList(nbytes);
		if (result.consumed > 0){
			assert(std::cmp_less_equal(result.consumed, approved));
			approved -= result.consumed;
		}

		return result;
	}

	void _ConsumeDirect(std::size_t nbytes) noexcept override {
		assert(std::cmp_less_equal(nbytes, approved));
		approved -= nbytes;

		ForwardIstream::_ConsumeDirect(nbytes);
	}

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		if (approved <= 0)
			return 0;

		if (std::cmp_greater(src.size(), approved))
			src = src.first((std::size_t)approved);

		auto nbytes = ForwardIstream::OnData(src);
		assert(std::cmp_less_equal(nbytes, approved));
		approved -= nbytes;
		return nbytes;
	}

	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override {
		if (approved <= 0)
			return IstreamDirectResult::BLOCKING;

		if (std::cmp_greater(max_length, approved)) {
			max_length = (std::size_t)approved;
			then_eof = false;
		}

		return ForwardIstream::OnDirect(type, fd, offset, max_length,
						then_eof);
	}
};

void
ApproveIstreamControl::Approve(uint_least64_t nbytes) noexcept
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

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "OptionalIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "istream_null.hxx"
#include "io/FileDescriptor.hxx"

#include <assert.h>

class OptionalIstream final : public ForwardIstream {
	const SharedPoolPtr<OptionalIstreamControl> control;

	bool resumed = false;

public:
	OptionalIstream(struct pool &p, UnusedIstreamPtr &&_input) noexcept
		:ForwardIstream(p, std::move(_input)),
		 control(SharedPoolPtr<OptionalIstreamControl>::Make(p, *this)) {}

	~OptionalIstream() noexcept override {
		control->optional = nullptr;
	}

	auto GetControl() noexcept {
		return control;
	}

	void Resume() noexcept {
		resumed = true;
	}

	void Discard() noexcept {
		assert(!resumed);

		resumed = true;

		/* replace the input with a "null" istream */
		ReplaceInputDirect(istream_null_new(GetPool()));
	}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override {
		/* can't respond to this until we're resumed, because the
		   original input can be discarded */
		return resumed ? ForwardIstream::_GetAvailable(partial) : -1;
	}

	void _Read() noexcept override {
		if (resumed)
			ForwardIstream::_Read();
	}

	void _FillBucketList(IstreamBucketList &list) override {
		if (resumed)
			ForwardIstream::_FillBucketList(list);
		else
			list.SetMore();
	}

	int _AsFd() noexcept override {
		return resumed
			? ForwardIstream::_AsFd()
			: -1;
	}

	/* handler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		return resumed
			? ForwardIstream::OnData(src)
			: 0;
	}

	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override {
		return resumed
			? ForwardIstream::OnDirect(type, fd, offset,
						   max_length, then_eof)
			: IstreamDirectResult::BLOCKING;
	}
};

void
OptionalIstreamControl::Resume() noexcept
{
	if (optional != nullptr)
		optional->Resume();
}

void
OptionalIstreamControl::Discard() noexcept
{
	if (optional != nullptr)
		optional->Discard();
}

std::pair<UnusedIstreamPtr, SharedPoolPtr<OptionalIstreamControl>>
istream_optional_new(struct pool &pool, UnusedIstreamPtr input) noexcept
{
	auto *i = NewIstream<OptionalIstream>(pool, std::move(input));
	return std::make_pair(UnusedIstreamPtr(i), i->GetControl());
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ByteIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "io/FileDescriptor.hxx"

class ByteIstream final : public ForwardIstream {
public:
	ByteIstream(struct pool &p, UnusedIstreamPtr _input)
		:ForwardIstream(p, std::move(_input)) {}

	/* virtual methods from class Istream */

	off_t _GetAvailable([[maybe_unused]] bool partial) noexcept override {
		return -1;
	}

	off_t _Skip([[maybe_unused]] off_t length) noexcept override {
		return -1;
	}

	void _FillBucketList(IstreamBucketList &list) override {
		IstreamBucketList tmp;
		ForwardIstream::_FillBucketList(tmp);
		list.SpliceBuffersFrom(std::move(tmp), 1);
	}

	int _AsFd() noexcept override {
		return -1;
	}

	/* handler */

	size_t OnData(std::span<const std::byte> src) noexcept override {
		return ForwardIstream::OnData(src.first(1));
	}

	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset,
				     [[maybe_unused]] size_t max_length,
				     bool then_eof) noexcept override {
		return ForwardIstream::OnDirect(type, fd, offset, 1,
						then_eof && max_length <= 1);
	}
};

UnusedIstreamPtr
istream_byte_new(struct pool &pool, UnusedIstreamPtr input)
{
	return NewIstreamPtr<ByteIstream>(pool, std::move(input));
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FourIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "Bucket.hxx"
#include "io/FileDescriptor.hxx"

#include <algorithm>

class FourIstream final : public ForwardIstream {
public:
	FourIstream(struct pool &p, UnusedIstreamPtr _input)
		:ForwardIstream(p, std::move(_input)) {}

	/* virtual methods from class Istream */

	IstreamLength _GetLength() noexcept override {
		auto result = ForwardIstream::_GetLength();
		if (result.length > 4) {
			result.length = 4;
			result.exhaustive = false;
		}

		return result;
	}

	void _FillBucketList(IstreamBucketList &list) override {
		IstreamBucketList tmp;
		ForwardIstream::_FillBucketList(tmp);

		const std::size_t max_size = 4;
		const std::size_t nbytes = list.SpliceBuffersFrom(std::move(tmp), max_size);
		if (nbytes >= max_size) {
			if (nbytes > max_size)
				/* there was more data in "tmp" */
				list.SetMore();
			else
				/* our input may have more data
				   eventually */
				list.CopyMoreFlagsFrom(tmp);
		}
	}

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		if (src.size() > 4)
			src = src.first(4);

		return ForwardIstream::OnData(src);
	}

	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override {
		return ForwardIstream::OnDirect(type, fd, offset,
						std::min(max_length, std::size_t{4}),
						then_eof && max_length <= 4);
	}
};

UnusedIstreamPtr
istream_four_new(struct pool *pool, UnusedIstreamPtr input) noexcept
{
	return NewIstreamPtr<FourIstream>(*pool, std::move(input));
}

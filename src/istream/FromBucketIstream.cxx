// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FromBucketIstream.hxx"
#include "Bucket.hxx"
#include "UnusedPtr.hxx"

FromBucketIstream::FromBucketIstream(struct pool &_pool,
				     UnusedIstreamPtr &&_input) noexcept
	:ForwardIstream(_pool, std::move(_input)) {}

void
FromBucketIstream::_Read() noexcept
{
	IstreamBucketList list;
	FillBucketListFromInput(list);
	if (list.IsEmpty())
		return;

	const DestructObserver destructed(*this);
	size_t total = 0;

	/* submit each bucket to InvokeData() */
	for (const auto &i : list) {
		// TODO: support more buffer types once they're implemented
		assert(i.IsBuffer());

		const auto buffer = i.GetBuffer();
		size_t consumed = InvokeData(buffer);
		if (consumed == 0 && destructed)
			return;

		assert(!destructed);

		total += consumed;

		if (consumed < buffer.size())
			break;
	}

	const auto c = input.ConsumeBucketList(total);
	assert(c.consumed == total);

	if (c.eof)
		DestroyEof();
}

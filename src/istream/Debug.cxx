// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef NDEBUG

#include "istream.hxx"
#include "Bucket.hxx"

void
Istream::FillBucketList(IstreamBucketList &list)
{
	assert(!destroyed);
	assert(!closing);
	assert(!eof);
	assert(!reading);
	assert(!in_data);

	const DestructObserver destructed(*this);
	reading = true;

	const std::size_t old_size = list.GetTotalBufferSize();

	try {
		_FillBucketList(list);
	} catch (...) {
		if (!destructed) {
			assert(destroyed);
		}

		throw;
	}

	assert(!destructed);
	assert(!destroyed);
	assert(reading);

	reading = false;

	const std::size_t new_size = list.GetTotalBufferSize();
	assert(new_size >= old_size);

	const std::size_t total_size = new_size - old_size;
	if ((off_t)total_size > available_partial)
		available_partial = total_size;

	if (!list.HasMore() && !list.HasNonBuffer()) {
		if (available_full_set)
			assert((off_t)total_size == available_full);
		else
			available_full = total_size;
	}
}

#endif // !NDEBUG

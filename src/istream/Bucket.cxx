// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Bucket.hxx"

void
IstreamBucketList::SpliceFrom(IstreamBucketList &&src) noexcept
{
	for (const auto &bucket : src)
		Push(bucket);

	if (!HasMore())
		CopyMoreFlagsFrom(src);
}

std::size_t
IstreamBucketList::SpliceBuffersFrom(IstreamBucketList &&src,
				     std::size_t max_size,
				     bool copy_more_flag) noexcept
{
	std::size_t total_size = 0;
	for (const auto &bucket : src) {
		if (max_size == 0 ||
		    !bucket.IsBuffer()) {
			if (copy_more_flag)
				SetMore();
			break;
		}

		auto buffer = bucket.GetBuffer();
		if (buffer.size() > max_size) {
			buffer = buffer.first(max_size);
			if (copy_more_flag)
				SetMore();
		}

		Push(buffer);
		max_size -= buffer.size();
		total_size += buffer.size();
	}

	if (!HasMore() && copy_more_flag)
		CopyMoreFlagsFrom(src);

	return total_size;
}

size_t
IstreamBucketList::SpliceBuffersFrom(IstreamBucketList &&src) noexcept
{
	std::size_t total_size = 0;
	for (const auto &bucket : src) {
		if (!bucket.IsBuffer()) {
			SetMore();
			break;
		}

		auto buffer = bucket.GetBuffer();
		Push(buffer);
		total_size += buffer.size();
	}

	if (!HasMore())
		CopyMoreFlagsFrom(src);

	return total_size;
}

std::size_t
IstreamBucketList::CopyBuffersFrom(std::size_t skip,
				   const IstreamBucketList &src) noexcept
{
	size_t total_size = 0;
	for (const auto &bucket : src) {
		if (!bucket.IsBuffer()) {
			SetMore();
			break;
		}

		auto buffer = bucket.GetBuffer();
		if (buffer.size() > skip) {
			buffer = buffer.subspan(skip);
			skip = 0;
			Push(buffer);
			total_size += buffer.size();
		} else
			skip -= buffer.size();
	}

	if (!HasMore())
		CopyMoreFlagsFrom(src);

	return total_size;
}

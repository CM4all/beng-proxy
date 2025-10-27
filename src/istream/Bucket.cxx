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
				     std::size_t max_size) noexcept
{
	std::size_t total_size = 0;
	for (const auto &bucket : src) {
		if (max_size == 0) {
			/* we have moved everything, but there is more
			   data: add 1 according to the API
			   contract */
			return total_size + 1;
		}

		if (!bucket.IsBuffer()) {
			EnableFallback();
			return total_size;
		}

		auto buffer = bucket.GetBuffer();
		if (buffer.size() > max_size) {
			buffer = buffer.first(max_size);
			Push(buffer);
			total_size += max_size;

			/* complete and there is more data, again: add
			   1 */
			return total_size + 1;
		}

		Push(buffer);
		max_size -= buffer.size();
		total_size += buffer.size();
	}

	if (!HasMore() && max_size > 0)
		CopyMoreFlagsFrom(src);

	return total_size;
}

size_t
IstreamBucketList::SpliceBuffersFrom(IstreamBucketList &&src) noexcept
{
	std::size_t total_size = 0;
	for (const auto &bucket : src) {
		if (!bucket.IsBuffer()) {
			EnableFallback();
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
			EnableFallback();
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

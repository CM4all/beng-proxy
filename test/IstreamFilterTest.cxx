// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "util/SpanCast.hxx"

bool
Context::ReadBuckets(std::size_t limit)
{
	if (abort_istream != nullptr)
		/* don't attempt to read buckets when this option is
		   set, because it's only properly implemented in
		   OnData() */
		return false;

	IstreamBucketList list;
	input.FillBucketList(list);

	if (list.IsEmpty() && list.HasMore())
		return false;

	got_data = true;

	bool result = true;
	std::size_t consumed = 0;

	for (const auto &i : list) {
		if (!i.IsBuffer()) {
			result = false;
			break;
		}

		const auto b = i.GetBuffer();
		std::size_t size = std::min(b.size(), limit);

		if (expected_result && record) {
			assert(skipped + buffer.size() == offset);
			assert(offset + b.size() <= strlen(expected_result));
			assert(memcmp((const char *)expected_result + skipped + buffer.size(),
				      b.data(), b.size()) == 0);

			buffer.append((const char *)b.data(), size);
		}

		consumed += size;
		offset += size;
		limit -= size;
		if (limit == 0)
			break;
	}

	[[maybe_unused]] const auto r = input.ConsumeBucketList(consumed);
	assert(r.consumed == consumed);
	// TODO check r.eof

	if (result && !list.HasMore()) {
		CloseInput();
		result = false;
	}

	return result;
}

/*
 * istream handler
 *
 */

std::size_t
Context::OnData(const std::span<const std::byte> src) noexcept
{
	std::size_t length = src.size();

	got_data = true;

	if (block_inject != nullptr) {
		DeferInject(*block_inject,
			    std::make_exception_ptr(std::runtime_error("block_inject")));
		block_inject = nullptr;
		return 0;
	}

	if (block_byte) {
		block_byte_state = !block_byte_state;
		if (block_byte_state)
			return 0;
	}

	if (abort_istream != nullptr)
		/* to ensure that the abort_after counter works
		   properly, throttle input */
		length = 1;

	if (abort_istream != nullptr && abort_after-- == 0) {
		DeferInject(*abort_istream,
			    std::make_exception_ptr(std::runtime_error("abort_istream")));
		abort_istream = nullptr;
		return 0;
	}

	if (half && length > 8)
		length = (length + 1) / 2;

	if (block_after >= 0) {
		--block_after;
		if (block_after == -1)
			/* block once */
			return 0;
	}

	if (expected_result && record) {
		assert(skipped + buffer.size() == offset);
		assert(offset + length <= strlen(expected_result));
		assert(memcmp((const char *)expected_result + skipped + buffer.size(),
			      src.data(), src.size()) == 0);

		buffer.append(ToStringView(src));
	}

	offset += length;

	if (close_after >= 0 && offset >= std::size_t(close_after)) {
		CloseInput();
		test_pool.reset(); // TODO: move this before CloseInput()
		eof = true;
		return 0;
	}

	return length;
}

IstreamDirectResult
Context::OnDirect(FdType, FileDescriptor, off_t, std::size_t max_length) noexcept
{
	got_data = true;

	if (block_inject != nullptr) {
		DeferInject(*block_inject,
			    std::make_exception_ptr(std::runtime_error("block_inject")));
		block_inject = nullptr;
		return IstreamDirectResult::END;
	}

	if (abort_istream != nullptr) {
		DeferInject(*abort_istream,
			    std::make_exception_ptr(std::runtime_error("abort_istream")));
		abort_istream = nullptr;
		return IstreamDirectResult::END;
	}

	offset += max_length;
	input.ConsumeDirect(max_length);
	return IstreamDirectResult::OK;
}

void
Context::OnEof() noexcept
{
	if (break_eof)
		instance.event_loop.Break();

	ClearInput();

	assert(test_pool);
	test_pool.reset();

	eof = true;
}

void
Context::OnError(std::exception_ptr) noexcept
{
	assert(!expected_result || !record);

	if (break_eof)
		instance.event_loop.Break();

	ClearInput();

	assert(test_pool);
	test_pool.reset();

	eof = true;
}

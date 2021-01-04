/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "IstreamFilterTest.hxx"

bool
Context::ReadBuckets(size_t limit)
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
	size_t consumed = 0;

	for (const auto &i : list) {
		if (!i.IsBuffer()) {
			result = false;
			break;
		}

		const auto b = i.GetBuffer();
		size_t size = std::min(b.size, limit);

		if (expected_result && record) {
			assert(skipped + buffer.size() == offset);
			assert(offset + b.size <= strlen(expected_result));
			assert(memcmp((const char *)expected_result + skipped + buffer.size(),
				      b.data, b.size) == 0);

			buffer.append((const char *)b.data, size);
		}

		consumed += size;
		offset += size;
		limit -= size;
		if (limit == 0)
			break;
	}

	gcc_unused size_t consumed2 = input.ConsumeBucketList(consumed);
	assert(consumed2 == consumed);

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

size_t
Context::OnData(gcc_unused const void *data, size_t length) noexcept
{
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
		assert(memcmp((const char *)expected_result + skipped + buffer.size(), data, length) == 0);

		buffer.append((const char *)data, length);
	}

	offset += length;

	if (close_after >= 0 && offset >= size_t(close_after)) {
		CloseInput();
		test_pool.reset(); // TODO: move this before CloseInput()
		eof = true;
		return 0;
	}

	return length;
}

ssize_t
Context::OnDirect(gcc_unused FdType type, gcc_unused int fd, size_t max_length) noexcept
{
	got_data = true;

	if (block_inject != nullptr) {
		DeferInject(*block_inject,
			    std::make_exception_ptr(std::runtime_error("block_inject")));
		block_inject = nullptr;
		return 0;
	}

	if (abort_istream != nullptr) {
		DeferInject(*abort_istream,
			    std::make_exception_ptr(std::runtime_error("abort_istream")));
		abort_istream = nullptr;
		return 0;
	}

	offset += max_length;
	return max_length;
}

void
Context::OnEof() noexcept
{
	ClearInput();

	assert(test_pool);
	test_pool.reset();

	eof = true;
}

void
Context::OnError(std::exception_ptr) noexcept
{
	assert(!expected_result || !record);

	ClearInput();

	assert(test_pool);
	test_pool.reset();

	eof = true;
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"

#include "util/SpanCast.hxx"

bool
Context::HandleBlockInject() noexcept
{
	if (block_inject == nullptr)
		return false;

	DeferInject(std::move(block_inject),
		    std::make_exception_ptr(std::runtime_error{"block_inject"}));
	block_inject = nullptr;
	return true;
}

void
Context::DeferInject(std::shared_ptr<InjectIstreamControl> &&inject,
		     std::exception_ptr ep) noexcept
{
	assert(ep);
	assert(defer_inject_istream == nullptr);
	assert(!defer_inject_error);

	defer_inject_istream = std::move(inject);
	defer_inject_error = ep;
	defer_inject_event.Schedule();
}

void
Context::DeferredInject() noexcept
{
	assert(defer_inject_istream != nullptr);
	assert(defer_inject_error);

	InjectFault(std::move(defer_inject_istream),
		    std::exchange(defer_inject_error, std::exception_ptr()));
}

std::pair<IstreamReadyResult, bool>
Context::ReadBuckets2(std::size_t limit, bool consume_more)
{
	if (abort_istream != nullptr)
		/* don't attempt to read buckets when this option is
		   set, because it's only properly implemented in
		   OnData() */
		return {IstreamReadyResult::OK, false};

	if (get_available_before_bucket) {
		/* these Istream::GetAvailable() are only here to trigger
		   assertions */
		[[maybe_unused]]
		const auto available_partial1 = input.GetAvailable(true),
			available_full1 = input.GetAvailable(false);
	}

	IstreamBucketList list;
	input.FillBucketList(list);

	if (get_available_after_bucket) {
		[[maybe_unused]]
		const auto available_partial2 = input.GetAvailable(true),
			available_full2 = input.GetAvailable(false);
	}

	if (list.ShouldFallback())
		bucket_fallback = true;

	if (list.IsEmpty() && list.HasMore())
		return {
			list.ShouldFallback() ? IstreamReadyResult::FALLBACK : IstreamReadyResult::OK,
			false
		};

	if (list.HasMore())
		consume_more = false;

	got_data = true;

	bool result = true;
	std::size_t consumed = 0;

	for (const auto &i : list) {
		if (!i.IsBuffer()) {
			consume_more = false;
			result = false;
			break;
		}

		const auto b = i.GetBuffer();
		std::size_t size = b.size();
		if (size > limit) {
			size = limit;
			consume_more = false;
		}

		if (options.expected_result.data() != nullptr && record) {
			assert(skipped + buffer.size() == offset);

			if (options.transform_result == nullptr) {
				assert(offset + b.size() <= options.expected_result.size());
				assert(memcmp(options.expected_result.data() + skipped + buffer.size(),
					      b.data(), b.size()) == 0);
			}

			buffer.append((const char *)b.data(), size);
		}

		consumed += size;
		offset += size;
		limit -= size;
		if (limit == 0) {
			consume_more = false;
			break;
		}
	}

	if (consumed + consume_more > 0) {
		[[maybe_unused]] const auto r = input.ConsumeBucketList(consumed + consume_more);
		assert(r.consumed == consumed);
		bucket_eof = eof = r.eof;
	} else if (list.IsEmpty()) {
		assert(!list.HasMore());
		bucket_eof = eof = true;
	}

	// TODO check r.eof

	[[maybe_unused]]
	const auto available_partial3 = input.GetAvailable(true),
		available_full3 = input.GetAvailable(false);

	IstreamReadyResult rresult = IstreamReadyResult::OK;

	if (result && !list.HasMore()) {
		if (ready_eof_ok)
			return {IstreamReadyResult::OK, result};

		CloseInput();
		result = false;
		rresult = IstreamReadyResult::CLOSED;
	}

	return {rresult, result};
}

bool
Context::ReadBuckets(std::size_t limit, bool consume_more)
{
	return ReadBuckets2(limit, consume_more).second;
}

void
Context::WaitForEndOfStream() noexcept
{
	assert(!break_eof);
	break_eof = true;

	while (!eof) {
		if (HasInput())
			input.Read();

		if (options.late_finish) {
			/* check a few options just in case OnData()
			   never gets called */

			if (HandleBlockInject())
				continue;

			if (abort_istream != nullptr && abort_after == 0) {
				DeferInject(std::move(abort_istream),
					    std::make_exception_ptr(std::runtime_error{"abort_istream"}));
				abort_istream = nullptr;
				continue;
			}
		}

		if (!eof) {
			instance.event_loop.Run();

			if (eof && HasInput() && ready_eof_ok)
				CloseInput();
		}
	}

	break_eof = false;

	assert(!HasInput());
	assert(eof);
}

/*
 * istream handler
 *
 */

IstreamReadyResult
Context::OnIstreamReady() noexcept
{
	if (HandleBlockInject())
		return IstreamReadyResult::OK;

	const auto result = on_ready_buckets
		? ReadBuckets2(1024 * 1024, false).first
		: IstreamReadyResult::FALLBACK;

	switch (result) {
	case IstreamReadyResult::OK:
	case IstreamReadyResult::FALLBACK:
		break;

	case IstreamReadyResult::CLOSED:
		instance.event_loop.Break();
		break;
	}

	return result;
}

std::size_t
Context::OnData(const std::span<const std::byte> src) noexcept
{
	if (break_ready) {
		instance.event_loop.Break();
		return 0;
	}

	std::size_t length = src.size();

	got_data = true;

	if (HandleBlockInject())
		return 0;

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
		DeferInject(std::move(abort_istream),
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

	if (options.expected_result.data() != nullptr && record) {
		assert(skipped + buffer.size() == offset);

		if (options.transform_result == nullptr) {
			assert(offset + length <= options.expected_result.size());
			assert(memcmp(options.expected_result.data() + skipped + buffer.size(),
				      src.data(), src.size()) == 0);
		}

		buffer.append(ToStringView(src.first(length)));
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
Context::OnDirect(FdType, FileDescriptor fd, off_t,
		  std::size_t max_length, bool then_eof) noexcept
{
	if (break_ready) {
		instance.event_loop.Break();
		return IstreamDirectResult::BLOCKING;
	}

	got_data = true;

	if (block_inject != nullptr) {
		DeferInject(std::move(block_inject),
			    std::make_exception_ptr(std::runtime_error("block_inject")));
		block_inject = nullptr;
		return IstreamDirectResult::BLOCKING;
	}

	if (abort_istream != nullptr) {
		DeferInject(std::move(abort_istream),
			    std::make_exception_ptr(std::runtime_error("abort_istream")));
		abort_istream = nullptr;
		return IstreamDirectResult::BLOCKING;
	}

	std::array<std::byte, 1024> tmp;
	std::span<std::byte> dest{tmp};
	if (dest.size() > max_length)
		dest = dest.first(max_length);
	const auto _nbytes = fd.Read(dest);
	if (_nbytes <= 0)
		return _nbytes < 0
			? IstreamDirectResult::ERRNO
			: IstreamDirectResult::END;

	const std::size_t nbytes = _nbytes;
	input.ConsumeDirect(nbytes);

	const std::span<const std::byte> src = dest.first(nbytes);

	if (options.expected_result.data() != nullptr && record) {
		assert(skipped + buffer.size() == offset);

		if (options.transform_result == nullptr) {
			assert(offset + nbytes <= options.expected_result.size());
			assert(memcmp(options.expected_result.data() + skipped + buffer.size(),
				      src.data(), src.size()) == 0);
		}

		buffer.append(ToStringView(src));
	}

	offset += nbytes;

	if (then_eof && nbytes == max_length) {
		if (break_eof)
			instance.event_loop.Break();

		CloseInput();
		assert(test_pool);
		test_pool.reset();

		eof = true;
		return IstreamDirectResult::CLOSED;
	}

	return IstreamDirectResult::OK;
}

void
Context::OnEof() noexcept
{
	if (break_eof || break_ready)
		instance.event_loop.Break();

	ClearInput();

	assert(test_pool);
	test_pool.reset();

	eof = true;
}

void
Context::OnError(std::exception_ptr) noexcept
{
	assert(options.expected_result.data() == nullptr || !record);

	if (break_eof || break_ready)
		instance.event_loop.Break();

	ClearInput();

	assert(test_pool);
	test_pool.reset();

	eof = true;
	error = true;
}

void
run_istream_ctx(Context &ctx)
{
	const AutoPoolCommit auto_pool_commit;

	ctx.eof = false;

	if (ctx.options.call_available) {
		[[maybe_unused]] off_t a1 = ctx.input.GetAvailable(false);
		[[maybe_unused]] off_t a2 = ctx.input.GetAvailable(true);
	}

	ctx.WaitForEndOfStream();

	if (ctx.options.expected_result.data() != nullptr && ctx.record &&
	    (ctx.options.transform_result == nullptr || ctx.skipped == 0)) {
		std::string_view result = ctx.buffer;

		std::string transformed;
		if (ctx.options.transform_result != nullptr)
			result = transformed = ctx.options.transform_result(result);

		ASSERT_EQ(result.size() + ctx.skipped,
			  ctx.options.expected_result.size());
		ASSERT_EQ(memcmp(result.data(),
				 ctx.options.expected_result.data() + ctx.skipped,
				 result.size()),
			  0);
	}
}

void
run_istream_block(const IstreamFilterTestOptions &options,
		  Instance &instance, PoolPtr pool,
		  UnusedIstreamPtr istream,
		  bool record,
		  int block_after)
{
	Context ctx(instance, std::move(pool),
		    options, std::move(istream));
	ctx.block_after = block_after;
	ctx.record = ctx.options.expected_result.data() != nullptr && record;

	run_istream_ctx(ctx);
}

void
run_istream(const IstreamFilterTestOptions &options,
	    Instance &instance, PoolPtr pool,
	    UnusedIstreamPtr istream, bool record)
{
	run_istream_block(options, instance, std::move(pool),
			  std::move(istream), record, -1);
}

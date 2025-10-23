// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "istream_hold.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "io/FileDescriptor.hxx"

#include <assert.h>

class HoldIstream final : public ForwardIstream {
	std::exception_ptr input_error;

public:
	HoldIstream(struct pool &p, UnusedIstreamPtr &&_input)
		:ForwardIstream(p, std::move(_input)) {}

private:
	bool Check() {
		if (HasInput()) [[likely]]
			return true;

		if (input_error) [[unlikely]]
			DestroyError(std::move(input_error));
		else
			DestroyEof();
		return false;
	}

public:
	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		if (HasInput())
			ForwardIstream::_SetDirect(mask);
	}

	IstreamLength _GetLength() noexcept override {
		if (HasInput()) [[likely]]
			return ForwardIstream::_GetLength();

		return {
			.length = 0,
			.exhaustive = !input_error,
		};
	}

	void _Read() noexcept override {
		if (Check()) [[likely]]
			ForwardIstream::_Read();
	}

	void _FillBucketList(IstreamBucketList &list) override {
		if (HasInput()) [[likely]] {
			ForwardIstream::_FillBucketList(list);
		} else if (input_error) [[unlikely]] {
			auto copy = input_error;
			Destroy();
			std::rethrow_exception(copy);
		}
	}

	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override {
		assert(!input_error);

		if (HasInput()) [[likely]]
			return ForwardIstream::_ConsumeBucketList(nbytes);
		else
			return {0, true};
	}

	void _Close() noexcept override {
		if (HasInput()) [[likely]]
			/* the input object is still there */
			ForwardIstream::_Close();
		else
			/* EOF or the handler is not interested in the error */
			Destroy();
	}

	/* virtual methods from class IstreamHandler */

	IstreamReadyResult OnIstreamReady() noexcept override {
		return HasHandler()
			? ForwardIstream::OnIstreamReady()
			: IstreamReadyResult::OK;
	}

	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		return HasHandler() ? ForwardIstream::OnData(src) : 0;
	}

	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override {
		return HasHandler()
			? ForwardIstream::OnDirect(type, fd, offset,
						   max_length, then_eof)
			: IstreamDirectResult::BLOCKING;
	}

	void OnEof() noexcept override {
		assert(HasInput());
		assert(!input_error);

		ClearInput();

		if (HasHandler())
			ForwardIstream::OnEof();
		else {
			/* queue the eof() call */
		}
	}

	void OnError(std::exception_ptr &&ep) noexcept override {
		assert(HasInput());
		assert(!input_error);

		ClearInput();

		if (HasHandler())
			ForwardIstream::OnError(std::move(ep));
		else
			/* queue the abort() call */
			input_error = std::move(ep);
	}
};

Istream *
istream_hold_new(struct pool &pool, UnusedIstreamPtr input)
{
	return NewIstream<HoldIstream>(pool, std::move(input));
}

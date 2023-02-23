// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
		if (gcc_likely(HasInput()))
			return true;

		if (gcc_unlikely(input_error))
			DestroyError(input_error);
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

	off_t _GetAvailable(bool partial) noexcept override {
		if (gcc_likely(HasInput()))
			return ForwardIstream::_GetAvailable(partial);

		return input_error ? -1 : 0;
	}

	off_t _Skip(off_t length) noexcept override {
		return gcc_likely(HasInput())
			? ForwardIstream::_Skip(length)
			: -1;
	}

	void _Read() noexcept override {
		if (gcc_likely(Check()))
			ForwardIstream::_Read();
	}

	void _FillBucketList(IstreamBucketList &list) override {
		if (gcc_likely(HasInput())) {
			try {
				input.FillBucketList(list);
			} catch (...) {
				Destroy();
				throw;
			}
		} else if (gcc_unlikely(input_error)) {
			auto copy = input_error;
			Destroy();
			std::rethrow_exception(copy);
		}
	}

	std::size_t _ConsumeBucketList(std::size_t nbytes) noexcept override {
		assert(!input_error);

		if (gcc_likely(HasInput()))
			return ForwardIstream::_ConsumeBucketList(nbytes);
		else
			return 0;
	}

	int _AsFd() noexcept override {
		return Check()
			? ForwardIstream::_AsFd()
			: -1;
	}

	void _Close() noexcept override {
		if (gcc_likely(HasInput()))
			/* the input object is still there */
			ForwardIstream::_Close();
		else
			/* EOF or the handler is not interested in the error */
			Destroy();
	}

	/* virtual methods from class IstreamHandler */

	bool OnIstreamReady() noexcept override {
		return !HasHandler() || ForwardIstream::OnIstreamReady();
	}

	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		return HasHandler() ? ForwardIstream::OnData(src) : 0;
	}

	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset,
				     std::size_t max_length) noexcept override {
		return HasHandler()
			? ForwardIstream::OnDirect(type, fd, offset,
						   max_length)
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

	void OnError(std::exception_ptr ep) noexcept override {
		assert(HasInput());
		assert(!input_error);

		ClearInput();

		if (HasHandler())
			ForwardIstream::OnError(ep);
		else
			/* queue the abort() call */
			input_error = ep;
	}
};

Istream *
istream_hold_new(struct pool &pool, UnusedIstreamPtr input)
{
	return NewIstream<HoldIstream>(pool, std::move(input));
}

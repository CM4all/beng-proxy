/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "istream_hold.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"

#include <assert.h>

class HoldIstream final : public ForwardIstream {
	bool input_eof = false;
	std::exception_ptr input_error;

public:
	HoldIstream(struct pool &p, UnusedIstreamPtr &&_input)
		:ForwardIstream(p, std::move(_input)) {}

private:
	bool Check() {
		if (gcc_unlikely(input_eof)) {
			DestroyEof();
			return false;
		} else if (gcc_unlikely(input_error)) {
			DestroyError(input_error);
			return false;
		} else
			return true;
	}

public:
	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override {
		if (gcc_unlikely(input_eof))
			return 0;
		else if (gcc_unlikely(input_error))
			return -1;

		return ForwardIstream::_GetAvailable(partial);
	}

	off_t _Skip(off_t length) noexcept override {
		return gcc_likely(!input_eof && !input_error)
			? ForwardIstream::_Skip(length)
			: -1;
	}

	void _Read() noexcept override {
		if (gcc_likely(Check()))
			ForwardIstream::_Read();
	}

	void _FillBucketList(IstreamBucketList &list) override {
		if (input_eof)
			return;
		else if (input_error) {
			auto copy = input_error;
			Destroy();
			std::rethrow_exception(copy);
		} else {
			try {
				input.FillBucketList(list);
			} catch (...) {
				Destroy();
				throw;
			}
		}
	}

	size_t _ConsumeBucketList(size_t nbytes) noexcept override {
		assert(!input_error);

		if (input_eof)
			return 0;
		else
			return ForwardIstream::_ConsumeBucketList(nbytes);
	}

	int _AsFd() noexcept override {
		return Check()
			? ForwardIstream::_AsFd()
			: -1;
	}

	void _Close() noexcept override {
		if (input_eof)
			Destroy();
		else if (input_error) {
			/* the handler is not interested in the error */
			Destroy();
		} else {
			/* the input object is still there */
			ForwardIstream::_Close();
		}
	}

	/* virtual methods from class IstreamHandler */

	bool OnIstreamReady() noexcept override {
		return !HasHandler() || ForwardIstream::OnIstreamReady();
	}

	size_t OnData(const void *data, size_t length) noexcept override {
		return HasHandler() ? ForwardIstream::OnData(data, length) : 0;
	}

	ssize_t OnDirect(FdType type, int fd,
			 size_t max_length) noexcept override {
		return HasHandler()
			? ForwardIstream::OnDirect(type, fd, max_length)
			: ssize_t(ISTREAM_RESULT_BLOCKING);
	}

	void OnEof() noexcept override {
		assert(!input_eof);
		assert(!input_error);

		if (HasHandler())
			ForwardIstream::OnEof();
		else
			/* queue the eof() call */
			input_eof = true;
	}

	void OnError(std::exception_ptr ep) noexcept override {
		assert(!input_eof);
		assert(!input_error);

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

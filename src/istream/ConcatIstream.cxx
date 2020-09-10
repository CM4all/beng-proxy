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

#include "ConcatIstream.hxx"
#include "Sink.hxx"
#include "Bucket.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "util/DestructObserver.hxx"
#include "util/WritableBuffer.hxx"

#include <boost/intrusive/slist.hpp>

#include <assert.h>

class CatIstream final : public Istream, DestructAnchor {
	struct Input final
		: IstreamSink,
		  boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

		CatIstream &cat;

		Input(CatIstream &_cat, UnusedIstreamPtr &&_istream) noexcept
			:IstreamSink(std::move(_istream)), cat(_cat) {}

		void SetDirect(FdTypeMask direct) noexcept {
			input.SetDirect(direct);
		}

		off_t GetAvailable(bool partial) const noexcept {
			return input.GetAvailable(partial);
		}

		off_t Skip(off_t length) noexcept {
			return input.Skip(length);
		}

		void Read() noexcept {
			input.Read();
		}

		void FillBucketList(IstreamBucketList &list) {
			input.FillBucketList(list);
		}

		size_t ConsumeBucketList(size_t nbytes) noexcept {
			return input.ConsumeBucketList(nbytes);
		}

		int AsFd() noexcept {
			return input.AsFd();
		}

		/* virtual methods from class IstreamHandler */

		bool OnIstreamReady() noexcept override {
			return cat.OnInputReady(*this);
		}

		size_t OnData(const void *data, size_t length) noexcept override {
			return cat.OnInputData(*this, data, length);
		}

		ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override {
			return cat.OnInputDirect(*this, type, fd, max_length);
		}

		void OnEof() noexcept override {
			assert(input.IsDefined());
			ClearInput();

			cat.OnInputEof(*this);
		}

		void OnError(std::exception_ptr ep) noexcept override {
			assert(input.IsDefined());
			ClearInput();

			cat.OnInputError(*this, ep);
		}

		struct Disposer {
			void operator()(Input *input) noexcept {
				input->input.Close();
			}
		};
	};

	bool reading = false;

	typedef boost::intrusive::slist<Input,
					boost::intrusive::constant_time_size<false>> InputList;
	InputList inputs;

public:
	CatIstream(struct pool &p, WritableBuffer<UnusedIstreamPtr> _inputs) noexcept;

private:
	Input &GetCurrent() noexcept {
		return inputs.front();
	}

	const Input &GetCurrent() const noexcept {
		return inputs.front();
	}

	bool IsCurrent(const Input &input) const noexcept {
		return &GetCurrent() == &input;
	}

	bool IsEOF() const noexcept {
		return inputs.empty();
	}

	void CloseAllInputs() noexcept {
		inputs.clear_and_dispose(Input::Disposer());
	}

	bool OnInputReady(Input &i) noexcept {
		return IsCurrent(i)
			? InvokeReady()
			: false;
	}

	size_t OnInputData(Input &i, const void *data, size_t length) noexcept {
		return IsCurrent(i)
			? InvokeData(data, length)
			: 0;
	}

	ssize_t OnInputDirect(gcc_unused Input &i, FdType type, int fd,
			      size_t max_length) noexcept {
		return IsCurrent(i)
			? InvokeDirect(type, fd, max_length)
			: (ssize_t)ISTREAM_RESULT_BLOCKING;
	}

	void OnInputEof(Input &i) noexcept {
		const bool current = IsCurrent(i);
		inputs.erase(inputs.iterator_to(i));

		if (IsEOF()) {
			assert(current);
			DestroyEof();
		} else if (current && !reading) {
			/* only call Input::_Read() if this function was not called
			   from CatIstream:Read() - in this case,
			   istream_cat_read() would provide the loop.  This is
			   advantageous because we avoid unnecessary recursing. */
			GetCurrent().Read();
		}
	}

	void OnInputError(Input &i, std::exception_ptr ep) noexcept {
		inputs.erase(inputs.iterator_to(i));
		CloseAllInputs();
		DestroyError(ep);
	}

public:
	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		for (auto &i : inputs)
			i.SetDirect(mask);
	}

	off_t _GetAvailable(bool partial) noexcept override;
	off_t _Skip(gcc_unused off_t length) noexcept override;
	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	size_t _ConsumeBucketList(size_t nbytes) noexcept override;
	int _AsFd() noexcept override;
	void _Close() noexcept override;
};

/*
 * istream implementation
 *
 */

off_t
CatIstream::_GetAvailable(bool partial) noexcept
{
	off_t available = 0;

	for (const auto &input : inputs) {
		const off_t a = input.GetAvailable(partial);
		if (a != (off_t)-1)
			available += a;
		else if (!partial)
			/* if the caller wants the exact number of bytes, and
			   one input cannot provide it, we cannot provide it
			   either */
			return (off_t)-1;
	}

	return available;
}

off_t
CatIstream::_Skip(off_t length) noexcept
{
	if (inputs.empty())
		return 0;

	off_t nbytes = inputs.front().Skip(length);
	Consumed(nbytes);
	return nbytes;
}

void
CatIstream::_Read() noexcept
{
	if (IsEOF()) {
		DestroyEof();
		return;
	}

	const DestructObserver destructed(*this);

	reading = true;

	CatIstream::InputList::const_iterator prev;
	do {
		prev = inputs.begin();
		GetCurrent().Read();
		if (destructed)
			return;
	} while (!IsEOF() && inputs.begin() != prev);

	reading = false;
}

void
CatIstream::_FillBucketList(IstreamBucketList &list)
{
	assert(!list.HasMore());

	for (auto &input : inputs) {
		try {
			input.FillBucketList(list);
		} catch (...) {
			inputs.erase(inputs.iterator_to(input));
			CloseAllInputs();
			Destroy();
			throw;
		}

		if (list.HasMore())
			break;
	}
}

size_t
CatIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	size_t total = 0;

	for (auto &input : inputs) {
		size_t consumed = input.ConsumeBucketList(nbytes);
		Consumed(consumed);
		total += consumed;
		nbytes -= consumed;
		if (nbytes == 0)
			break;
	}

	return total;
}

int
CatIstream::_AsFd() noexcept
{
	/* we can safely forward the as_fd() call to our input if it's the
	   last one */

	if (std::next(inputs.begin()) != inputs.end())
		/* not on last input */
		return -1;

	auto &i = GetCurrent();
	int fd = i.AsFd();
	if (fd >= 0)
		Destroy();

	return fd;
}

void
CatIstream::_Close() noexcept
{
	CloseAllInputs();
	Destroy();
}

/*
 * constructor
 *
 */

inline CatIstream::CatIstream(struct pool &p, WritableBuffer<UnusedIstreamPtr> _inputs) noexcept
	:Istream(p)
{
	auto i = inputs.before_begin();

	for (UnusedIstreamPtr &_input : _inputs) {
		if (!_input)
			continue;

		auto *input = NewFromPool<Input>(p, *this, std::move(_input));
		i = inputs.insert_after(i, *input);
	}
}

UnusedIstreamPtr
_istream_cat_new(struct pool &pool, UnusedIstreamPtr *const inputs, unsigned n_inputs)
{
	return UnusedIstreamPtr(NewFromPool<CatIstream>(pool, pool,
							WritableBuffer<UnusedIstreamPtr>(inputs, n_inputs)));
}

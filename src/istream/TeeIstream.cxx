/*
 * Copyright 2007-2019 CM4all GmbH
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

#include "TeeIstream.hxx"
#include "UnusedPtr.hxx"
#include "Pointer.hxx"
#include "Bucket.hxx"
#include "Handler.hxx"
#include "New.hxx"
#include "pool/pool.hxx"
#include "event/DeferEvent.hxx"
#include "util/Cast.hxx"
#include "util/DestructObserver.hxx"

#include <boost/intrusive/list.hpp>

#include <stdexcept>

#include <assert.h>

namespace bi = boost::intrusive;

struct TeeIstream final : IstreamHandler, DestructAnchor {

	struct Output
		: bi::list_base_hook<bi::link_mode<bi::normal_link>>,
		  Istream, DestructAnchor
	{
		TeeIstream &parent;

		/**
		 * The number of bytes to skip for this output.  This output
		 * has already consumed this many bytes, but the following
		 * outputs blocked.
		 */
		size_t skip = 0;

		/**
		 * The number of bytes provided by _FillBucketList().  This is
		 * a kludge that is explained in _ConsumeBucketList().
		 */
		size_t bucket_list_size;

		/**
		 * A weak output is one which is closed automatically when all
		 * "strong" outputs have been closed - it will not keep up the
		 * istream_tee object alone.
		 */
		const bool weak;

		Output(struct pool &p, TeeIstream &_parent, bool _weak) noexcept
			:Istream(p), parent(_parent), weak(_weak) {}

		~Output() noexcept override {
			parent.Remove(*this);
		}

		Output(const Output &) = delete;
		Output &operator=(const Output &) = delete;

		size_t Feed(const char *data, size_t length) noexcept;

		void Consumed(size_t nbytes) noexcept {
			assert(nbytes <= skip);
			skip -= nbytes;
		}

		friend struct TeeIstream;

		/* virtual methods from class Istream */

		off_t _GetAvailable(bool partial) noexcept override {
			auto available = parent.input.GetAvailable(partial);
			if (available >= 0) {
				assert(available >= (off_t)skip);
				available -= skip;
			}

			return available;
		}

		void _Read() noexcept override {
			parent.ReadInput();
		}

		void _FillBucketList(IstreamBucketList &list) override {
			if (!parent.IsFirst(*this)) {
				/* (for now) allow only the first output to read
				   buckets, because implementing it for the other
				   outputs is rather complicated */
				list.SetMore();
				bucket_list_size = 0;
				return;
			}

			if (skip > 0) {
				/* TODO: this can be optimized by skipping data from
				   new buckets */
				list.SetMore();
				bucket_list_size = 0;
				return;
			}

			IstreamBucketList sub;

			try {
				parent.input.FillBucketList(sub);
			} catch (...) {
				parent.PostponeError(std::current_exception());
				Destroy();
				throw;
			}

			bucket_list_size = list.SpliceBuffersFrom(sub);

			if (!parent.IsSingleOutput())
				/* if there are more outputs, they may
				   not get an OnData() callback for
				   the data we have just loaded into
				   the bucket list, so let's schedule
				   a read */
				parent.DeferRead();
		}

		size_t _ConsumeBucketList(size_t nbytes) noexcept override {
			assert(skip == 0);

			/* we must not call tee.input.ConsumeBucketList() because
			   that would discard data which must still be sent to the
			   second output; instead of doing that, we still remember
			   how much data our input pushed to the list, and we
			   consume this portion of "bytes" */

			size_t consumed = std::min(nbytes, bucket_list_size);
			Istream::Consumed(consumed);
			skip = consumed;
			return consumed;
		}

		void _Close() noexcept override;
	};

	using OutputList =
		bi::list<Output,
			 bi::base_hook<bi::list_base_hook<bi::link_mode<bi::normal_link>>>,
			 bi::constant_time_size<false>>;

	OutputList outputs;

	OutputList::iterator next_output = outputs.end();

	unsigned n_strong = 0;

	IstreamPointer input;

	/**
	 * This event is used to defer an input.Read() call.
	 */
	DeferEvent defer_event;

	/**
	 * Caught by Output::_FillBucketList().
	 */
	std::exception_ptr postponed_error;

	TeeIstream(UnusedIstreamPtr _input, EventLoop &event_loop,
		   bool defer_read) noexcept
		:input(std::move(_input), *this),
		 defer_event(event_loop, BIND_THIS_METHOD(ReadInput))
	{
		if (defer_read)
			DeferRead();
	}

	void Destroy() noexcept {
		this->~TeeIstream();
	}

	struct pool &GetPool() noexcept {
		assert(!outputs.empty());

		return outputs.front().GetPool();
	}

	UnusedIstreamPtr CreateOutput(struct pool &p, bool weak) noexcept {
		assert(outputs.empty() || &p == &outputs.front().GetPool());

		auto *output = NewIstream<Output>(p, *this, weak);
		outputs.push_back(*output);
		if (!weak)
			++n_strong;
		return UnusedIstreamPtr(output);
	}

	UnusedIstreamPtr CreateOutput(bool weak) noexcept {
		return CreateOutput(GetPool(), weak);
	}

	bool IsSingleOutput() const noexcept {
		assert(!outputs.empty());

		return std::next(outputs.begin()) == outputs.end();
	}

	void ReadInput() noexcept {
		assert(!outputs.empty());

		if (postponed_error) {
			assert(!input.IsDefined());

			defer_event.Cancel();

			const DestructObserver destructed(*this);
			while (!outputs.empty()) {
				outputs.front().DestroyError(postponed_error);
				if (destructed)
					return;
			}

			return;
		}

		input.Read();
	}

	void DeferRead() noexcept {
		assert(input.IsDefined() || postponed_error);

		defer_event.Schedule();
	}

	void PostponeError(std::exception_ptr e) noexcept {
		assert(!postponed_error);
		postponed_error = std::move(e);
		DeferRead();
	}

	bool IsFirst(const Output &output) const noexcept {
		assert(!outputs.empty());
		return &output == &outputs.front();
	}

	void Remove(Output &output) noexcept;

	/* virtual methods from class IstreamHandler */
	size_t OnData(const void *data, size_t length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

inline size_t
TeeIstream::Output::Feed(const char *data, size_t length) noexcept
{
	if (length <= skip)
		/* all of this has already been sent to this output, but
		   following outputs one didn't accept it yet */
		return length;

	/* skip the part which was already sent */
	data += skip;
	length -= skip;

	const DestructObserver destructed(*this);
	const DestructObserver parent_destructed(parent);

	size_t nbytes = InvokeData(data, length);
	if (destructed) {
		/* this output has been closed, so pretend everything
		   was consumed (unless the whole TeeIstream has been
		   destroyed) */
		assert(nbytes == 0);
		return parent_destructed ? 0 : length;
	}

	skip += nbytes;
	return skip;
}

void
TeeIstream::Output::_Close() noexcept
{
	Destroy();
}

void
TeeIstream::Remove(Output &output) noexcept
{
	auto i = outputs.iterator_to(output);
	if (next_output == i)
		++next_output;
	outputs.erase(i);

	if (!output.weak)
		--n_strong;

	if (!input.IsDefined()) {
		/* this can happen during OnEof() or OnError(); and over
		   there, this #TeeIstream and its remaining outputs will be
		   destructed properly, so we can just do nothing here */
		if (outputs.empty())
			Destroy();
		return;
	}

	if (n_strong > 0) {
		assert(!outputs.empty());

		DeferRead();
		return;
	}

	input.ClearAndClose();
	defer_event.Cancel();

	if (outputs.empty()) {
		Destroy();
		return;
	}

	const DestructObserver destructed(*this);

	while (!outputs.empty()) {
		outputs.front().DestroyError(std::make_exception_ptr(std::runtime_error("closing the weak second output")));
		if (destructed)
			return;
	}
}

/*
 * istream handler
 *
 */

size_t
TeeIstream::OnData(const void *data, size_t length) noexcept
{
	assert(input.IsDefined());

	for (auto i = outputs.begin(); i != outputs.end(); i = next_output) {
		next_output = std::next(i);

		size_t nbytes = i->Feed((const char *)data, length);
		if (nbytes == 0)
			return 0;

		if (nbytes < length)
			length = nbytes;
	}

	for (auto &i : outputs)
		i.Consumed(length);

	return length;
}

void
TeeIstream::OnEof() noexcept
{
	assert(input.IsDefined());
	input.Clear();
	defer_event.Cancel();

	const DestructObserver destructed(*this);

	/* clean up in reverse order */

	while (!outputs.empty()) {
		outputs.back().DestroyEof();
		if (destructed)
			return;
	}
}

void
TeeIstream::OnError(std::exception_ptr ep) noexcept
{
	assert(input.IsDefined());
	input.Clear();
	defer_event.Cancel();

	const DestructObserver destructed(*this);

	/* clean up in reverse order */

	while (!outputs.empty()) {
		outputs.back().DestroyError(ep);
		if (destructed)
			return;
	}
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
NewTeeIstream(struct pool &pool, UnusedIstreamPtr input,
	      EventLoop &event_loop,
	      bool weak,
	      bool defer_read) noexcept
{
	auto tee = NewFromPool<TeeIstream>(pool, std::move(input),
					   event_loop,
					   defer_read);
	return tee->CreateOutput(pool, weak);
}

UnusedIstreamPtr
AddTeeIstream(UnusedIstreamPtr &_tee, bool weak) noexcept
{
	auto &tee = _tee.StaticCast<TeeIstream::Output>();

	return tee.parent.CreateOutput(weak);
}

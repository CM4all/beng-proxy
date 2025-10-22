// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ReplaceIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "Sink.hxx"
#include "pool/pool.hxx"
#include "util/DestructObserver.hxx"

#include <stdexcept>

#include <assert.h>

struct ReplaceIstream::Substitution final : IntrusiveForwardListHook, IstreamSink {
	ReplaceIstream &replace;
	const off_t start;
	off_t end;

	Substitution(ReplaceIstream &_replace,
		     off_t _start, off_t _end,
		     UnusedIstreamPtr &&_input) noexcept;

	bool IsDefined() const noexcept {
		return input.IsDefined();
	}

	IstreamLength GetLength() const noexcept {
		return input.GetLength();
	}

	void Read() noexcept {
		input.Read();
	}

	void FillBucketList(IstreamBucketList &list) {
		if (!IsDefined())
			return;

		input.FillBucketList(list);
	}

	auto ConsumeBucketList(size_t nbytes) noexcept {
		assert(IsActive());
		return IsDefined()
			? input.ConsumeBucketList(nbytes)
			: ConsumeBucketResult{0, true};
	}

	[[gnu::pure]]
	bool IsActive() const noexcept;

	/* virtual methods from class IstreamHandler */

	IstreamReadyResult OnIstreamReady() noexcept override;
	size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

ReplaceIstream::Substitution::Substitution(ReplaceIstream &_replace,
					   off_t _start, off_t _end,
					   UnusedIstreamPtr &&_input) noexcept
	:IstreamSink(std::move(_input)),
	 replace(_replace),
	 start(_start), end(_end)
{
}

/**
 * Is this substitution object is active, i.e. its data is the next
 * being written?
 */
inline bool
ReplaceIstream::Substitution::IsActive() const noexcept
{
	assert(!replace.substitutions.empty());
	assert(replace.substitutions.front().start <= start);
	assert(start >= replace.position);

	return replace.substitutions.iterator_to(*this) == replace.substitutions.begin() && replace.position == start;
}

void
ReplaceIstream::ToNextSubstitution(ReplaceIstream::Substitution &s) noexcept
{
	assert(substitutions.iterator_to(s) == substitutions.begin());
	assert(s.IsActive());
	assert(s.start <= s.end);

	buffer.Skip(s.end - s.start);
	position = s.end;

	substitutions.pop_front_and_dispose(PoolDisposer{GetPool()});

	assert(substitutions.empty() ||
	       substitutions.front().start >= position);
}

/*
 * istream handler
 *
 */

IstreamReadyResult
ReplaceIstream::Substitution::OnIstreamReady() noexcept
{
	if (!IsActive())
		return IstreamReadyResult::OK;

	return replace.OnSubstitutionReady(*this);
}

size_t
ReplaceIstream::Substitution::OnData(std::span<const std::byte> src) noexcept
{
	if (IsActive()) {
		replace.had_output = true;
		return replace.InvokeData(src);
	} else
		return 0;
}

void
ReplaceIstream::Substitution::OnEof() noexcept
{
	input.Clear();

	if (IsActive()) {
		/* copy the "replace" reference into our stack frame
		   because ToNextSubstitution() will destruct this
		   object */
		auto &r = replace;

		r.ToNextSubstitution(*this);

		if (r.IsEOF())
			r.DestroyEof();
		else
			r.defer_read.Schedule();
	}
}

void
ReplaceIstream::Substitution::OnError(std::exception_ptr ep) noexcept
{
	ClearInput();

	replace.DestroyError(ep);
}

/*
 * misc methods
 *
 */

ReplaceIstream::ReplaceIstream(struct pool &p, EventLoop &event_loop,
			       UnusedIstreamPtr _input) noexcept
	:FacadeIstream(p, std::move(_input)),
	 defer_read(event_loop, BIND_THIS_METHOD(DeferredRead))
{
}

ReplaceIstream::~ReplaceIstream() noexcept
{
	substitutions.clear_and_dispose(PoolDisposer{GetPool()});
}

inline off_t
ReplaceIstream::GetBufferEndOffsetUntil(off_t _position, SubstitutionList::const_iterator s) const noexcept
{
	if (s != substitutions.end())
		return std::min(s->start, source_length);
	else if (finished)
		return source_length;
	else if (_position < settled_position)
		return settled_position;
	else
		/* block after the last substitution, unless
		   the caller has already set the "finished"
		   flag */
		return -1;
}

bool
ReplaceIstream::ReadSubstitution() noexcept
{
	while (!substitutions.empty() && substitutions.front().IsActive()) {
		auto &s = substitutions.front();

		if (s.IsDefined()) {
			const DestructObserver destructed(*this);

			s.Read();

			if (destructed)
				return false;

			/* we assume the substitution object is blocking if it hasn't
			   reached EOF with this one call */
			if (substitutions.iterator_to(s) == substitutions.begin())
				return false;
		} else {
			ToNextSubstitution(s);
			if (IsEOF()) {
				DestroyEof();
				return false;
			}
		}
	}

	return true;
}

inline size_t
ReplaceIstream::ReadFromBuffer(size_t max_length) noexcept
{
	assert(max_length > 0);

	auto src = buffer.Read();
	assert(!src.empty());

	if (src.size() > max_length)
		src = src.first(max_length);

	had_output = true;
	size_t nbytes = InvokeData(src);
	assert(nbytes <= src.size());

	if (nbytes == 0)
		/* istream_replace has been closed */
		return src.size();

	buffer.Consume(nbytes);
	position += nbytes;

	assert(position <= source_length);

	return src.size() - nbytes;
}

inline bool
ReplaceIstream::ReadFromBufferLoop(off_t end) noexcept
{
	assert(end >= position);
	assert(end <= source_length);

	/* this loop is required to cross the GrowingBuffer borders */
	while (position < end) {
		size_t max_length = (size_t)(end - position);
		if (ReadFromBuffer(max_length) > 0)
			return false;

		assert(position <= end);
	}

	return true;
}

bool
ReplaceIstream::TryReadFromBuffer() noexcept
{
	assert(position <= source_length);

	off_t end = GetBufferEndOffsetUntil(substitutions.begin());
	if (end < 0)
		return true;

	assert(end >= position);
	assert(end <= source_length);

	if (!ReadFromBufferLoop(end))
		return false;

	assert(position == end);

	if (position == source_length &&
	    substitutions.empty() &&
	    !input.IsDefined()) {
		DestroyEof();
		return false;
	}

	return true;
}

inline void
ReplaceIstream::DeferredRead() noexcept
{
	switch (InvokeReady()) {
	case IstreamReadyResult::OK:
		break;

	case IstreamReadyResult::FALLBACK:
		TryRead();
		break;

	case IstreamReadyResult::CLOSED:
		break;
	}
}

bool
ReplaceIstream::TryRead() noexcept
{
	assert(position <= source_length);

	/* read until someone (input or output) blocks */
	do {
		if (!ReadSubstitution())
			return false;

		if (!TryReadFromBuffer())
			return false;
	} while (!substitutions.empty() &&
		 /* quit the loop if we don't have enough data yet */
		 substitutions.front().start <= source_length);

	return true;
}


void
ReplaceIstream::AppendToBuffer(const std::span<const std::byte> src)
{
	if (source_length >= 8 * 1024 * 1024)
		throw std::runtime_error{"file too large for processor"};

	buffer.Write(src);
	source_length += (off_t)src.size();

	Parse(src);
}

/*
 * input handler
 *
 */

IstreamReadyResult
ReplaceIstream::OnIstreamReady() noexcept
{
	defer_read.Cancel();

	auto result = InvokeReady();
	if (result != IstreamReadyResult::CLOSED && !HasInput())
		/* our input has been closed; we need to return CLOSED
		   to it, even though ReplaceIstream's handler was
		   successful and didn't close the ReplaceIstream */
		result = IstreamReadyResult::CLOSED;

	return result;
}

size_t
ReplaceIstream::OnData(const std::span<const std::byte> src) noexcept
{
	had_input = true;

	const auto old_source_length = source_length;

	try {
		AppendToBuffer(src);
	} catch (...) {
		DestroyError(std::current_exception());
		return 0;
	}

	if (GetBufferEndOffsetUntil(substitutions.begin()) > old_source_length) {
		defer_read.Schedule();
		had_output = true;
	}

	return src.size();
}

void
ReplaceIstream::OnEof() noexcept
{
	input.Clear();

	try {
		ParseEnd();
	} catch (...) {
		DestroyError(std::current_exception());
		return;
	}

	assert(finished);

	if (IsEOF())
		DestroyEof();
	else
		defer_read.Schedule();
}

void
ReplaceIstream::OnError(std::exception_ptr ep) noexcept
{
	input.Clear();
	DestroyError(ep);
}

/*
 * istream implementation
 *
 */

IstreamLength
ReplaceIstream::_GetLength() noexcept
{
	/* get available bytes from input */

	IstreamLength result{
		.length = 0,
		.exhaustive = finished,
	};

	if (HasInput() && finished)
		result = input.GetLength();

	/* add available bytes from substitutions (and the source buffers
	   before the substitutions) */

	off_t position2 = position;

	for (const auto &subst : substitutions) {
		assert(position2 <= subst.start);

		result.length += subst.start - position2;

		if (subst.IsDefined())
			result += subst.GetLength();

		position2 = subst.end;
	}

	/* add available bytes from tail (if known yet) */

	if (finished)
		result.length += source_length - position2;
	else if (position2 < settled_position)
		result.length += settled_position - position2;

	return result;
}

void
ReplaceIstream::_Read() noexcept
{
	if (!TryRead())
		return;

	if (!HasInput())
		return;

	const DestructObserver destructed(*this);

	had_output = false;

	do {
		had_input = false;
		input.Read();
	} while (!destructed && had_input && !had_output && HasInput());
}

void
ReplaceIstream::_FillBucketList(IstreamBucketList &list)
{
	assert(!list.HasMore());

	defer_read.Cancel();

	if (HasInput()) {
		/* fill our buffer from the input */
		IstreamBucketList tmp;
		FillBucketListFromInput(tmp);

		if (tmp.ShouldFallback())
			list.EnableFallback();

		size_t total = 0;
		bool only_buffers = true;
		for (const auto &i : tmp) {
			if (i.GetType() != IstreamBucket::Type::BUFFER) {
				list.EnableFallback();
				only_buffers = false;
				break;
			}

			const auto b = i.GetBuffer();

			try {
				AppendToBuffer(b);
			} catch (...) {
				Destroy();
				throw;
			}

			total += b.size();
		}

		if (only_buffers && !tmp.HasMore()) {
			CloseInput();

			try {
				ParseEnd();
			} catch (...) {
				Destroy();
				throw;
			}
		} else if (total > 0)
			input.ConsumeBucketList(total);
	}

	off_t fill_position = position;
	for (auto s = substitutions.begin();; ++s) {
		off_t end = GetBufferEndOffsetUntil(fill_position, s);
		if (end < 0) {
			/* after last substitution and the "settled" position:
			   not yet ready to read */
			list.SetMore();
			return;
		}

		assert(end >= fill_position);
		assert(end <= source_length);

		if (end > fill_position) {
			/* supply data from the buffer until the first
			   substitution */
			const size_t before_size = end - fill_position;

			IstreamBucketList tmp;
			buffer.FillBucketList(tmp, fill_position - position);
			size_t nbytes = list.SpliceBuffersFrom(std::move(tmp),
							       before_size,
							       false);
			if (nbytes < before_size) {
				list.SetMore();
				return;
			}
		}

		if (s == substitutions.end()) {
			if (input.IsDefined() || !finished)
				list.SetMore();
			return;
		}

		if (end < s->start) {
			list.SetMore();
			return;
		}

		IstreamBucketList tmp;

		try {
			s->FillBucketList(tmp);
		} catch (...) {
			Destroy();
			throw;
		}

		if (tmp.IsEmpty() && !tmp.HasMore() && s->IsActive()) {
			/* this (active) substitution is empty -
			   remove it and try again; getting rid of
			   empty substitutions is important because
			   otherwise OnIstreamReady() calls from the
			   following substitutions would be ignored;
			   while reading buckets, nobody else would do
			   that */
			assert(fill_position == position);
			assert(end == fill_position);

			ToNextSubstitution(*s);

			// TODO refactor the loop to avoid this recursive call
			list.SetMore(false);
			return _FillBucketList(list);
		}

		list.SpliceBuffersFrom(std::move(tmp));
		if (tmp.HasMore())
			return;

		fill_position = s->end;
	}
}

Istream::ConsumeBucketResult
ReplaceIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	defer_read.Cancel();

	size_t total = 0;

	while (true) {
		const auto s = substitutions.begin();
		off_t end = GetBufferEndOffsetUntil(s);
		assert(end >= 0);

		if (end > position) {
			/* consume data from the buffer until the first
			   substitution */
			const size_t before_size = end - position;

			if (nbytes <= before_size) {
				/* consumed less than the available chunk */
				total += nbytes;
				position += nbytes;
				buffer.Skip(nbytes);
				Consumed(nbytes);
				break;
			}

			nbytes -= before_size;
			total += before_size;
			position += before_size;
			buffer.Skip(before_size);
			Consumed(before_size);
		}

		if (s == substitutions.end())
			break;

		const auto r = s->ConsumeBucketList(nbytes);
		Consumed(r.consumed);
		total += r.consumed;
		nbytes -= r.consumed;

		if (r.eof)
			ToNextSubstitution(*s);

		if (nbytes == 0)
			break;

		/* if there is still data to be consumed, it must mean
		   that the substitution Istream has reached the
		   end */
		assert(r.eof);
	}

	return {total, IsEOF()};
}

void
ReplaceIstream::Add(off_t start, off_t end,
		    UnusedIstreamPtr contents) noexcept
{
	assert(!finished);
	assert(start >= 0);
	assert(start <= end);
	assert(start >= settled_position);
	assert(start >= last_substitution_end);

	if (!contents && start == end)
		return;

	auto s = NewFromPool<Substitution>(GetPool(), *this, start, end,
					   std::move(contents));

	settled_position = end;

#ifndef NDEBUG
	last_substitution_end = end;
#endif

	substitutions.push_back(*s);

	defer_read.Schedule();
}

void
ReplaceIstream::Extend([[maybe_unused]] off_t start, off_t end) noexcept
{
	assert(!finished);
	assert(!substitutions.empty());

	auto &substitution = substitutions.back();
	assert(substitution.start == start);
	assert(substitution.end == settled_position);
	assert(substitution.end == last_substitution_end);
	assert(end >= substitution.end);

	substitution.end = end;
	settled_position = end;
#ifndef NDEBUG
	last_substitution_end = end;
#endif
}

void
ReplaceIstream::Settle(off_t offset) noexcept
{
	assert(!finished);
	assert(offset >= settled_position);

	settled_position = offset;

	defer_read.Schedule();
}

void
ReplaceIstream::Finish() noexcept
{
	assert(!finished);

	finished = true;

	defer_read.Schedule();
}

IstreamReadyResult
ReplaceIstream::OnSubstitutionReady(Substitution &s) noexcept
{
	assert(s.IsActive());

	defer_read.Cancel();

	auto result = InvokeReady();
	if (result != IstreamReadyResult::CLOSED && substitutions.iterator_to(s) != substitutions.begin())
		/* this substitution has been closed; we need to
		   return CLOSED to its callback, even though
		   ReplaceIstream's handler was successful and didn't
		   close the ReplaceIstream */
		result = IstreamReadyResult::CLOSED;

	return result;
}

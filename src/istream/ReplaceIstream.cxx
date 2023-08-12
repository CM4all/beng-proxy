// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ReplaceIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "pool/pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/DestructObserver.hxx"

#include <stdexcept>

#include <assert.h>

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
bool
ReplaceIstream::Substitution::IsActive() const noexcept
{
	assert(replace.first_substitution != nullptr);
	assert(replace.first_substitution->start <= start);
	assert(start >= replace.position);

	return this == replace.first_substitution && replace.position == start;
}

void
ReplaceIstream::ToNextSubstitution(ReplaceIstream::Substitution *s) noexcept
{
	assert(first_substitution == s);
	assert(s->IsActive());
	assert(s->start <= s->end);

	buffer.Skip(s->end - s->start);
	position = s->end;

	first_substitution = s->next;
	if (first_substitution == nullptr) {
		assert(append_substitution_p == &s->next);
		append_substitution_p = &first_substitution;
	}

	s->Destroy();

	assert(first_substitution == nullptr ||
	       first_substitution->start >= position);
}

/*
 * istream handler
 *
 */

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
		replace.ToNextSubstitution(this);

		if (replace.IsEOF())
			replace.DestroyEof();
		else
			replace.defer_read.Schedule();
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
	while (first_substitution != nullptr) {
		auto *s = first_substitution;
		first_substitution = s->next;

		s->Destroy();
	}
}


bool
ReplaceIstream::ReadSubstitution() noexcept
{
	while (first_substitution != nullptr && first_substitution->IsActive()) {
		auto *s = first_substitution;

		if (s->IsDefined()) {
			const DestructObserver destructed(*this);

			s->Read();

			if (destructed)
				return false;

			/* we assume the substitution object is blocking if it hasn't
			   reached EOF with this one call */
			if (s == first_substitution)
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

	off_t end = GetBufferEndOffsetUntil(first_substitution);
	if (end < 0)
		return true;

	assert(end >= position);
	assert(end <= source_length);

	if (!ReadFromBufferLoop(end))
		return false;

	assert(position == end);

	if (position == source_length &&
	    first_substitution == nullptr &&
	    !input.IsDefined()) {
		DestroyEof();
		return false;
	}

	return true;
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
	} while (first_substitution != nullptr &&
		 /* quit the loop if we don't have enough data yet */
		 first_substitution->start <= source_length);

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

	if (GetBufferEndOffsetUntil(first_substitution) > old_source_length) {
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

off_t
ReplaceIstream::_GetAvailable(bool partial) noexcept
{
	if (!partial && !finished)
		/* we don't know yet how many substitutions will come, so we
		   cannot calculate the exact rest */
		return (off_t)-1;

	/* get available bytes from input */

	off_t input_avalable = 0;
	if (HasInput() && finished) {
		input_avalable = input.GetAvailable(partial);
		if (input_avalable == (off_t)-1) {
			if (!partial)
				return (off_t)-1;
			input_avalable = 0;
		}
	}

	/* add available bytes from substitutions (and the source buffers
	   before the substitutions) */

	off_t available = input_avalable;
	off_t position2 = position;

	for (auto subst = first_substitution;
	     subst != nullptr; subst = subst->next) {
		assert(position2 <= subst->start);

		available += subst->start - position2;

		if (subst->IsDefined()) {
			const off_t l = subst->GetAvailable(partial);
			if (l != (off_t)-1)
				available += l;
			else if (!partial)
				return (off_t)-1;
		}

		position2 = subst->end;
	}

	/* add available bytes from tail (if known yet) */

	if (finished)
		available += source_length - position2;

	return available;
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

	if (HasInput()) {
		/* fill our buffer from the input */
		IstreamBucketList tmp;

		try {
			input.FillBucketList(tmp);
		} catch (...) {
			input.Clear();
			Destroy();
			throw;
		}

		size_t total = 0;
		bool only_buffers = true;
		for (const auto &i : tmp) {
			if (i.GetType() != IstreamBucket::Type::BUFFER) {
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
	for (auto *s = first_substitution;; s = s->next) {
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

		if (s == nullptr) {
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

		list.SpliceBuffersFrom(std::move(tmp));
		if (tmp.HasMore())
			return;

		fill_position = s->end;
	}
}

Istream::ConsumeBucketResult
ReplaceIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	size_t total = 0;

	while (true) {
		auto *s = first_substitution;
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

		if (s == nullptr)
			break;

		const auto r = s->ConsumeBucketList(nbytes);
		Consumed(r.consumed);
		total += r.consumed;
		nbytes -= r.consumed;

		if (r.eof)
			ToNextSubstitution(s);

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

	*append_substitution_p = s;
	append_substitution_p = &s->next;

	defer_read.Schedule();
}

inline ReplaceIstream::Substitution *
ReplaceIstream::GetLastSubstitution() noexcept
{
	auto *substitution = first_substitution;
	assert(substitution != nullptr);

	while (substitution->next != nullptr)
		substitution = substitution->next;

	assert(substitution->end <= settled_position);
	assert(substitution->end == last_substitution_end);
	return substitution;
}

void
ReplaceIstream::Extend([[maybe_unused]] off_t start, off_t end) noexcept
{
	assert(!finished);

	auto *substitution = GetLastSubstitution();
	assert(substitution->start == start);
	assert(substitution->end == settled_position);
	assert(substitution->end == last_substitution_end);
	assert(end >= substitution->end);

	substitution->end = end;
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

	auto result = InvokeReady();
	if (result != IstreamReadyResult::CLOSED && first_substitution != &s)
		/* this substitution has been closed; we need to
		   return CLOSED to its callback, even though
		   ReplaceIstream's handler was successful and didn't
		   close the ReplaceIstream */
		result = IstreamReadyResult::CLOSED;

	return result;
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "FacadeIstream.hxx"
#include "Sink.hxx"
#include "event/DeferEvent.hxx"
#include "memory/GrowingBuffer.hxx"

#include <sys/types.h>

struct pool;
class EventLoop;
class Istream;
class UnusedIstreamPtr;

class ReplaceIstream : public FacadeIstream, DestructAnchor {
	struct Substitution final : IstreamSink {
		Substitution *next = nullptr;
		ReplaceIstream &replace;
		const off_t start;
		off_t end;

		Substitution(ReplaceIstream &_replace,
			     off_t _start, off_t _end,
			     UnusedIstreamPtr &&_input) noexcept;

		void Destroy() noexcept {
			this->~Substitution();
		}

		bool IsDefined() const noexcept {
			return input.IsDefined();
		}

		off_t GetAvailable(bool partial) const noexcept {
			return input.GetAvailable(partial);
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

	/**
	 * This event is scheduled when a method call
	 * call allows us to submit more data to the #IstreamHandler.
	 * This avoids stalling the transfer when the last Read() call did
	 * not return any data.
	 */
	DeferEvent defer_read;

	bool finished = false;
	bool had_input, had_output;

	GrowingBuffer buffer;
	off_t source_length = 0, position = 0;

	/**
	 * The offset given by istream_replace_settle() or the end offset
	 * of the last substitution (whichever is bigger).
	 */
	off_t settled_position = 0;

	Substitution *first_substitution = nullptr,
		**append_substitution_p = &first_substitution;

#ifndef NDEBUG
	off_t last_substitution_end = 0;
#endif

public:
	ReplaceIstream(struct pool &p, EventLoop &event_loop,
		       UnusedIstreamPtr _input) noexcept;

	~ReplaceIstream() noexcept override;

	void Add(off_t start, off_t end, UnusedIstreamPtr contents) noexcept;
	void Extend(off_t start, off_t end) noexcept;
	void Settle(off_t offset) noexcept;
	void Finish() noexcept;

protected:
	using FacadeIstream::GetPool;

private:
	using FacadeIstream::HasInput;

	/**
	 * Throws on error.
	 */
	void AppendToBuffer(const std::span<const std::byte> src);

	/**
	 * Is the buffer at the end-of-file position?
	 */
	bool IsBufferAtEOF() const noexcept {
		return position == source_length;
	}

	/**
	 * Is the object at end-of-file?
	 */
	bool IsEOF() const noexcept {
		return !input.IsDefined() && finished &&
			first_substitution == nullptr &&
			IsBufferAtEOF();
	}

	[[gnu::pure]]
	off_t GetBufferEndOffsetUntil(off_t _position, const Substitution *s) const noexcept {
		if (s != nullptr)
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

	[[gnu::pure]]
	off_t GetBufferEndOffsetUntil(const Substitution *s) const noexcept {
		return GetBufferEndOffsetUntil(position, s);
	}

	/**
	 * Copy the next chunk from the source buffer to the istream
	 * handler.
	 *
	 * @return true if all pending data has been consumed, false if
	 * the handler is blocking or if this object has been destroyed
	 */
	bool TryReadFromBuffer() noexcept;

	void DeferredRead() noexcept;

	/**
	 * Copy data from the source buffer to the istream handler.
	 *
	 * @return 0 if the istream handler is not blocking; the number of
	 * bytes remaining in the buffer if it is blocking
	 */
	size_t ReadFromBuffer(size_t max_length) noexcept;

	/**
	 * @return true if all data until #end has been consumed, false if
	 * the handler is blocking or if this object has been destroyed
	 */
	bool ReadFromBufferLoop(off_t end) noexcept;

	/**
	 * @return true if all pending data has been consumed, false if
	 * the handler is blocking or if this object has been destroyed
	 */
	bool TryRead() noexcept;

	/**
	 * Read data from substitution objects.
	 *
	 * @return true if there is no active substitution and reading
	 * shall continue; false if the active substitution blocks or this
	 * object was destroyed
	 */
	bool ReadSubstitution() noexcept;

	/**
	 * Activate the next substitution object after s.
	 */
	void ToNextSubstitution(ReplaceIstream::Substitution *s) noexcept;

	Substitution *GetLastSubstitution() noexcept;

	IstreamReadyResult OnSubstitutionReady(Substitution &s) noexcept;

protected:
	/**
	 * This method is called after new input data has been
	 * appended to the buffer.  It may be used to parse the new
	 * data and add substitutions.
	 *
	 * On error, it may throw an exception which will be forwarded
	 * to our handler's OnError() method, but this method must not
	 * destroy this #ReplaceIstream instance.
	 */
	virtual void Parse(std::span<const std::byte> b) {
		(void) b;
	}

	/**
	 * This method is called after all data has been passed to
	 * Parse() at the end of all input.  This method must either
	 * fail or must "finish" this #ReplaceIstream instance by
	 * calling Finish().
	 *
	 * On error, it may throw an exception which will be forwarded
	 * to our handler's OnError() method, but this method must not
	 * destroy this #ReplaceIstream instance.
	 */
	virtual void ParseEnd() {
	}

public:
	/* virtual methods from class IstreamHandler */
	IstreamReadyResult OnIstreamReady() noexcept override;
	size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Istream */
	off_t _GetAvailable(bool partial) noexcept override;
	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(size_t nbytes) noexcept override;
};

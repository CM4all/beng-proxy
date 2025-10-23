// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Length.hxx"
#include "Result.hxx"
#include "pool/Holder.hxx"
#include "io/FdType.hxx"
#include "util/DestructObserver.hxx"
#include "util/LeakDetector.hxx"

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <exception> // for std::exception_ptr
#include <span>

#include <limits.h> // for INT_MAX

class FileDescriptor;
class IstreamHandler;
class IstreamBucketList;

/**
 * This trick avoids the "DestructAnchor is an ambiguous base of ..."
 * error.
 */
class IstreamDestructAnchor {
#ifndef NDEBUG
	DestructAnchor destruct_anchor;

public:
	operator DestructAnchor &() noexcept {
		return destruct_anchor;
	}
#endif
};

/**
 * An asynchronous input stream.
 *
 * The lifetime of an #Istream begins when it is created, and ends
 * with one of the following events:
 *
 * - it is closed manually using Close()
 * - it has reached end-of-file (when IstreamHandler::OnEof() is called)
 * - an error has occurred (when IstreamHandler::OnError() is called)
 */
class Istream : PoolHolder, LeakDetector, IstreamDestructAnchor {
	/** data sink */
	IstreamHandler *handler = nullptr;

#ifndef NDEBUG
	bool reading = false, destroyed = false;

	bool closing = false, eof = false, bucket_eof = false, bucket_eof_seen = false;

	bool in_data = false, available_full_set = false;

	bool in_direct = false;

	/** how much data was available in the previous invocation? */
	std::size_t data_available = 0;

	/**
	 * Sum of all recent Consumed() calls.  This is used for
	 * assertions in ConsumeBucketList().
	 */
	std::size_t consumed_sum;

	uint_least64_t available_partial = 0, available_full = 0;
#endif

protected:
	template<typename P>
	explicit Istream(P &&_pool TRACE_ARGS_DEFAULT) noexcept
		:PoolHolder(std::forward<P>(_pool) TRACE_ARGS_FWD) {}

	Istream(const Istream &) = delete;
	Istream &operator=(const Istream &) = delete;

	virtual ~Istream() noexcept;

	using PoolHolder::GetPool;

	static constexpr std::pair<std::size_t, bool> CalcMaxDirect(std::integral auto remaining) noexcept {
		/* Linux can't splice() more than 2 GB at a time and
		   may return EINVAL if we ask it to transfer more */
		if (remaining > INT_MAX)
			return {INT_MAX, false};

		return {static_cast<std::size_t>(remaining), true};
	}

	std::size_t Consumed(std::size_t nbytes) noexcept {
#ifndef NDEBUG
		consumed_sum += nbytes;

		if (std::cmp_greater_equal(nbytes, available_partial))
			available_partial = 0;
		else
			available_partial -= nbytes;

		if (available_full_set) {
			assert(std::cmp_less_equal(nbytes, available_full));

			available_full -= nbytes;
		}

		data_available -= std::min(nbytes, data_available);
#endif
		return nbytes;
	}

	IstreamReadyResult InvokeReady() noexcept;
	std::size_t InvokeData(std::span<const std::byte> src) noexcept;
	IstreamDirectResult InvokeDirect(FdType type, FileDescriptor fd,
					 off_t offset, std::size_t max_length,
					 bool then_eof) noexcept;
	void InvokeEof() noexcept;
	void InvokeError(std::exception_ptr ep) noexcept;

	/**
	 * Prepare a call to IstreamHandler::OnEof(); the caller is
	 * responsible for actually calling it.
	 */
	IstreamHandler &PrepareEof() noexcept;

	/**
	 * Prepare a call to IstreamHandler::OnError(); the caller is
	 * response for actually calling it.
	 */
	IstreamHandler &PrepareError() noexcept;

	void Destroy() noexcept {
		this->~Istream();
		/* no need to free memory from the pool */
	}

	void DestroyEof() noexcept;
	void DestroyError(std::exception_ptr ep) noexcept;

	/**
	 * @return the number of bytes still in the buffer
	 */
	template<typename Buffer>
	std::size_t ConsumeFromBuffer(Buffer &buffer) noexcept {
		auto r = buffer.Read();
		if (r.empty())
			return 0;

		std::size_t consumed = InvokeData(r);
		if (consumed > 0)
			buffer.Consume(consumed);
		return r.size() - consumed;
	}

	/**
	 * @return the number of bytes consumed
	 */
	template<typename Buffer>
	std::size_t SendFromBuffer(Buffer &buffer) noexcept {
		auto r = buffer.Read();
		if (r.empty())
			return 0;

		std::size_t consumed = InvokeData(r);
		if (consumed > 0)
			buffer.Consume(consumed);
		return consumed;
	}

public:
	bool HasHandler() const noexcept {
		assert(!destroyed);

		return handler != nullptr;
	}

	void SetHandler(IstreamHandler &_handler) noexcept {
		assert(!destroyed);

		handler = &_handler;
	}

	/**
	 * Detach the handler from this object.  This should only be done
	 * if it is going to be reattached to a new handler right after
	 * this call.
	 */
	void ClearHandler() noexcept {
		handler = nullptr;
		SetDirect(0);
	}

	void SetDirect(FdTypeMask mask) noexcept {
		assert(!destroyed);

		_SetDirect(mask);
	}

	/**
	 * How long is the remainder of this #Istream?
	 */
	[[gnu::pure]]
	IstreamLength GetLength() noexcept {
#ifndef NDEBUG
		assert(!destroyed);
		assert(!closing);
		assert(!eof);
		assert(!reading);

		const DestructObserver destructed(*this);
		reading = true;
#endif

		const auto result = _GetLength();

#ifndef NDEBUG
		assert(!destructed);
		assert(!destroyed);
		assert(reading);

		reading = false;

		assert(available_partial == 0 || result.length >= available_partial);
		if (result.length > available_partial)
			available_partial = result.length;

		if (result.exhaustive) {
			assert(!available_full_set ||
			       available_full == result.length);
			available_full = result.length;
			available_full_set = true;
		}
#endif

		return result;
	}

	/**
	 * Skip data without processing it.  By skipping 0 bytes, you can
	 * test whether the stream is able to skip at all.
	 *
	 * @return the number of bytes skipped or -1 if skipping is not supported
	 */
	off_t Skip(off_t length) noexcept {
#ifndef NDEBUG
		assert(!destroyed);
		assert(!closing);
		assert(!eof);
		assert(!bucket_eof);
		assert(!reading);

		const DestructObserver destructed(*this);
		reading = true;
		in_direct = false;
#endif

		off_t nbytes = _Skip(length);
		assert(nbytes <= length);

#ifndef NDEBUG
		if (destructed || destroyed)
			return nbytes;

		reading = false;

		if (nbytes > 0) {
			if (std::cmp_greater(nbytes, available_partial))
				available_partial = 0;
			else
				available_partial -= nbytes;

			assert(!available_full_set ||
			       std::cmp_less(nbytes, available_full));
			if (available_full_set)
				available_full -= nbytes;
		}
#endif

		return nbytes;
	}

	/**
	 * Try to read from the stream.  If the stream can read data
	 * without blocking, it must provide data.  It may invoke the
	 * callbacks any number of times, supposed that the handler itself
	 * doesn't block.
	 *
	 * If the stream does not provide data immediately (and it is not
	 * at EOF yet), it must install an event and invoke the handler
	 * later, whenever data becomes available.
	 *
	 * Whenever the handler reports it is blocking, the responsibility
	 * for calling back (and calling this function) is handed back to
	 * the istream handler.
	 */
	void Read() noexcept  {
#ifndef NDEBUG
		assert(!destroyed);
		assert(!closing);
		assert(!eof);
		assert(!bucket_eof);
		assert(!reading);
		assert(!in_data);

		const DestructObserver destructed(*this);
		reading = true;
		in_direct = false;
#endif

		_Read();

#ifndef NDEBUG
		if (destructed || destroyed)
			return;

		reading = false;
#endif
	}

	/**
	 * Append #IstreamBucket instances with consecutive data from this
	 * #Istream to the end of the given #IstreamBucketList.  Unless
	 * the returned data marks the end of the stream,
	 * IstreamBucketList::SetMore() must be called.
	 *
	 * On error, this method destroys the #Istream instance and throws
	 * std::runtime_error.
	 */
#ifdef NDEBUG
	void FillBucketList(IstreamBucketList &list) {
		_FillBucketList(list);
	}
#else
	void FillBucketList(IstreamBucketList &list);
#endif

	struct ConsumeBucketResult {
		/**
		 * The number of bytes really consumed by this instance
		 * (the rest will be consumed by its siblings).
		 */
		std::size_t consumed;

		/**
		 * Has this #Istream reached end-of-file?  If not,
		 * then more data may (or may not) be available later.
		 */
		bool eof;
	};

	/**
	 * Consume data from the #IstreamBucketList filled by
	 * FillBucketList().
	 *
	 * @param nbytes the number of bytes to be consumed; may be more
	 * than returned by FillBucketList(), because some of the data may
	 * be returned by this Istream's successive siblings
	 */
	ConsumeBucketResult ConsumeBucketList(std::size_t nbytes) noexcept {
#ifndef NDEBUG
		assert(!destroyed);
		assert(!closing);
		assert(!eof);
		assert(!bucket_eof);
		assert(!reading);
		assert(!in_data);

		consumed_sum = 0;
#endif

		auto result = _ConsumeBucketList(nbytes);

#ifndef NDEBUG
		assert(!destroyed);
		assert(!bucket_eof);
		assert(result.consumed <= nbytes);
		assert(consumed_sum == result.consumed);
		assert(result.eof || result.consumed == nbytes);
		assert(!result.eof || available_partial == 0);

		if (bucket_eof_seen) {
			assert(available_full_set);

			if (result.eof) {
				assert(available_partial == 0);
				assert(available_full == 0);
			} else {
				assert(available_partial > 0);
				assert(available_full > 0);
			}
		} else {
			assert(!result.eof);
		}

		bucket_eof = result.eof;
#endif

		return result;
	}

	/**
	 * Consume data from the file descriptor passed to
	 * IstreamHandler::OnDirect().
	 *
	 * @param nbytes the number of bytes which were consumed
	 */
	void ConsumeDirect(std::size_t nbytes) noexcept {
#ifndef NDEBUG
		assert(nbytes > 0);
		assert(!destroyed);
		assert(!closing);
		assert(!eof);
		assert(!bucket_eof);
		assert(in_direct);

		consumed_sum = 0;
#endif

		_ConsumeDirect(Consumed(nbytes));

#ifndef NDEBUG
		assert(!destroyed);
		assert(consumed_sum == nbytes);
#endif
	}

	/**
	 * Close the stream and free resources.  This must not be called
	 * after the handler's eof() / abort() callbacks were invoked.
	 */
	void Close() noexcept {
#ifndef NDEBUG
		assert(!destroyed);
		assert(!closing);
		assert(!eof);

		closing = true;
#endif

		_Close();
	}

	/**
	 * Close an istream which was never used, i.e. it does not have a
	 * handler yet.
	 */
	void CloseUnused() noexcept {
		assert(!HasHandler());

		Close();
	}

protected:
	ConsumeBucketResult Consumed(ConsumeBucketResult r) noexcept {
		Consumed(r.consumed);
		return r;
	}

	/**
	 * This method can be implemented by subclasses to propagate
	 * the new tag to their inputs.
	 */
	virtual void _SetDirect([[maybe_unused]] FdTypeMask _handler_direct) noexcept {
	}

	virtual IstreamLength _GetLength() noexcept {
		return {0, false};
	}

	virtual off_t _Skip([[maybe_unused]] off_t length) noexcept {
		return -1;
	}

	virtual void _Read() noexcept = 0;

	virtual void _FillBucketList(IstreamBucketList &list);
	virtual ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept;
	virtual void _ConsumeDirect(std::size_t nbytes) noexcept;

	virtual void _Close() noexcept {
		Destroy();
	}
};

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream/DechunkIstream.hxx"
#include "istream/istream.hxx"
#include "istream/Bucket.hxx"

#include <utility> // for std::cmp_greater_equal()

#include <assert.h>
#include <stddef.h>

struct pool;
class EventLoop;
class SocketDescriptor;

/**
 * Utilities for reading a HTTP body, either request or response.
 */
class HttpBodyReader : public Istream, protected DechunkHandler {
	/**
	 * The remaining size is unknown.
	 */
	static constexpr off_t REST_UNKNOWN = -1;

	/**
	 * EOF chunk has been seen.
	 */
	static constexpr off_t REST_EOF_CHUNK = -2;

	/**
	 * Chunked response.  Will flip to #REST_EOF_CHUNK as soon
	 * as the EOF chunk is seen.
	 */
	static constexpr off_t REST_CHUNKED = -3;

	/**
	 * The remaining number of bytes.
	 *
	 * @see #REST_UNKNOWN, #REST_EOF_CHUNK,
	 * #REST_CHUNKED
	 */
	off_t rest;

	FdTypeMask direct_mask = 0;

	bool end_seen;

public:
	template<typename P>
	explicit HttpBodyReader(P &&_pool) noexcept
		:Istream(std::forward<P>(_pool)) {}

	UnusedIstreamPtr Init(EventLoop &event_loop, off_t content_length,
			      bool chunked) noexcept;

	using Istream::GetPool;
	using Istream::Destroy;
	using Istream::InvokeReady;

	IstreamHandler *PrepareEof() noexcept {
		/* suppress InvokeEof() if rest==REST_EOF_CHUNK because in
		   that case, the dechunker has already emitted that event */
		return rest == 0
			? &Istream::PrepareEof()
			: nullptr;
	}

	void InvokeEof() noexcept {
		/* suppress InvokeEof() if rest==REST_EOF_CHUNK because in
		   that case, the dechunker has already emitted that event */
		if (rest == 0)
			Istream::InvokeEof();
	}

	void DestroyEof() noexcept {
		InvokeEof();
		Destroy();
	}

	using Istream::InvokeError;
	using Istream::DestroyError;

	bool IsChunked() const noexcept {
		return rest == REST_CHUNKED || rest == REST_EOF_CHUNK;
	}

	/**
	 * Do we know the remaining length of the body?
	 */
	bool KnownLength() const noexcept {
		return rest >= 0;
	}

	bool IsEOF() const noexcept {
		return rest == 0 || rest == REST_EOF_CHUNK;
	}

	bool GotEndChunk() const noexcept {
		return rest == REST_EOF_CHUNK;
	}

	/**
	 * Do we require more data to finish the body?
	 */
	bool RequireMore() const noexcept {
		return rest > 0 || (rest == REST_CHUNKED && !end_seen);
	}

	template<typename Socket>
	[[gnu::pure]]
	IstreamLength GetLength(const Socket &s) const noexcept {
		assert(rest != REST_EOF_CHUNK);

		if (KnownLength())
			return {
				.length = static_cast<uint_least64_t>(rest),
				.exhaustive = true,
			};

		return {
			.length = s.GetAvailable(),
			.exhaustive = false,
		};
	}

	template<typename Socket>
	void FillBucketList(const Socket &s,
			    IstreamBucketList &list) noexcept {
		const auto b = s.ReadBuffer();
		if (b.empty()) {
			if (!IsEOF())
				list.SetMore();
			return;
		}

		const auto [t, then_eof] = TruncateInput(b);
		list.Push(t);
		if (!then_eof)
			list.SetMore();
	}

	template<typename Socket>
	ConsumeBucketResult ConsumeBucketList(Socket &s, std::size_t nbytes) noexcept {
		auto b = s.ReadBuffer();
		std::size_t max = GetMaxRead(b.size());
		if (nbytes > max)
			nbytes = max;
		if (nbytes == 0)
			return {0, IsEOF()};

		s.DisposeConsumed(nbytes);
		s.AfterConsumed();
		Consumed(nbytes);

		if (nbytes > 0 && !IsEOF() && s.IsConnected())
			s.ScheduleRead();

		return {Istream::Consumed(nbytes), IsEOF()};
	}

	std::size_t FeedBody(std::span<const std::byte> src) noexcept;

	bool CheckDirect(FdType type) const noexcept {
		return (direct_mask & FdTypeMask(type)) != 0;
       }

	IstreamDirectResult TryDirect(SocketDescriptor fd, FdType fd_type) noexcept;

	/**
	 * Determines whether the socket can be released now.  This is true if
	 * the body is empty, or if the data in the buffer contains enough for
	 * the full response.
	 */
	template<typename Socket>
	[[gnu::pure]]
	bool IsSocketDone(const Socket &s) const noexcept {
		if (IsChunked())
			return end_seen;

		return KnownLength() &&
			std::cmp_greater_equal(s.GetAvailable(), rest);
	}

	/**
	 * The underlying socket has been closed by the remote.
	 *
	 * @return true if there is data left in the buffer, false if the body
	 * has been finished (with or without error)
	 */
	bool SocketEOF(std::size_t remaining) noexcept;

	/**
	 * Discard data from the input buffer.  This method shall be used
	 * to discard an unwanted request body.
	 *
	 * @return true if the whole body has been removed from the input
	 * buffer
	 */
	template<typename Socket>
	bool Discard(Socket &s) noexcept {
		if (IsChunked() || !KnownLength())
			return false;

		/* note: using s.ReadBuffer().size instead of s.GetAvailable()
		   to work around a problem with
		   ThreadSocketFilter::Consumed() which asserts that
		   ReadBuffer() has moved decrypted_input into
		   unprotected_decrypted_input */
		std::size_t available = s.ReadBuffer().size();
		if (std::cmp_less(available, rest))
			return false;

		s.DisposeConsumed(rest);
		return true;
	}

private:
	[[gnu::pure]]
	std::size_t GetMaxRead(std::size_t length) const noexcept;

	/**
	 * Truncate data from the input buffer to the known remaining
	 * length.
	 *
	 * @return the truncated span and a flag indicating whether
	 * the body reaches end-of-file after that
	 */
	[[gnu::pure]]
	std::pair<std::span<const std::byte>, bool> TruncateInput(std::span<const std::byte> i) noexcept {
		assert(rest != REST_EOF_CHUNK);

		bool then_eof = false;
		if (KnownLength() && std::cmp_less_equal(rest, i.size())) {
			i = i.first(static_cast<std::size_t>(rest));
			then_eof = true;
		}

		return {i, then_eof};
	}

	void Consumed(std::size_t nbytes) noexcept;

public:
	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct_mask = mask;
	}

	void _ConsumeDirect(std::size_t nbytes) noexcept override {
		Consumed(nbytes);
	}

protected:
	/* virtual methods from class DechunkHandler */
	void OnDechunkEndSeen() noexcept override;
	DechunkInputAction OnDechunkEnd() noexcept override;
};

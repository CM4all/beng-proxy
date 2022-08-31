/*
 * Copyright 2007-2022 CM4all GmbH
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

#pragma once

#include "istream/DechunkIstream.hxx"
#include "istream/istream.hxx"
#include "istream/Bucket.hxx"

#include <assert.h>
#include <stddef.h>

struct pool;
class EventLoop;
class SocketDescriptor;

/**
 * Utilities for reading a HTTP body, either request or response.
 */
class HttpBodyReader : public Istream, DechunkHandler {
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
	off_t GetAvailable(const Socket &s, bool partial) const noexcept {
		assert(rest != REST_EOF_CHUNK);

		if (KnownLength())
			return rest;

		return partial
			? (off_t)s.GetAvailable()
			: -1;
	}

	template<typename Socket>
	void FillBucketList(const Socket &s,
			    IstreamBucketList &list) noexcept {
		auto b = s.ReadBuffer();
		if (b.empty()) {
			if (!IsEOF())
				list.SetMore();
			return;
		}

		std::size_t max = GetMaxRead(b.size());
		if (b.size() > max)
			b = b.first(max);

		list.Push(b);
		if ((off_t)b.size() != rest)
			list.SetMore();
	}

	template<typename Socket>
	std::size_t ConsumeBucketList(Socket &s, std::size_t nbytes) noexcept {
		auto b = s.ReadBuffer();
		if (b.empty())
			return 0;

		std::size_t max = GetMaxRead(b.size());
		if (nbytes > max)
			nbytes = max;
		if (nbytes == 0)
			return 0;

		s.DisposeConsumed(nbytes);
		Consumed(nbytes);
		return Istream::Consumed(nbytes);
	}

	std::size_t FeedBody(const void *data, std::size_t length) noexcept;

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

		return KnownLength() && (off_t)s.GetAvailable() >= rest;
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
		if ((off_t)available < rest)
			return false;

		s.DisposeConsumed(rest);
		return true;
	}

private:
	[[gnu::pure]]
	std::size_t GetMaxRead(std::size_t length) const noexcept;

	void Consumed(std::size_t nbytes) noexcept;

public:
	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct_mask = mask;
	}

	void _ConsumeDirect(std::size_t) noexcept override {
	}

protected:
	/* virtual methods from class DechunkHandler */
	void OnDechunkEndSeen() noexcept final;
	bool OnDechunkEnd() noexcept final;
};

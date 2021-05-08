/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "ForwardIstream.hxx"

/**
 * An #Istream proxy which which provides a known length.  This can be
 * used by a HTTP client to propagate the Content-Length response
 * header, for example.
 */
class LengthIstream final : public ForwardIstream {
	off_t remaining;

public:
	template<typename P, typename I>
	LengthIstream(P &&_pool, I &&_input, off_t _length)
		:ForwardIstream(std::forward<P>(_pool),
				std::forward<I>(_input)),
		 remaining(_length) {}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool) noexcept override;
	off_t _Skip(off_t length) noexcept override;
	size_t _ConsumeBucketList(size_t nbytes) noexcept override;

protected:
	/* virtual methods from class IstreamHandler */

	size_t OnData(const void *data, size_t length) noexcept override;
	ssize_t OnDirect(FdType type, int fd,
			 size_t max_length) noexcept override;
	void OnEof() noexcept override;
};

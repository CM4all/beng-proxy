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

#include "FacadeIstream.hxx"

class ForwardIstream : public FacadeIstream {
protected:
	template<typename I>
	ForwardIstream(struct pool &_pool, I &&_input)
		:FacadeIstream(_pool, std::forward<I>(_input)) {}

	explicit ForwardIstream(struct pool &_pool)
		:FacadeIstream(_pool) {}

public:
	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		input.SetDirect(mask);
	}

	off_t _GetAvailable(bool partial) noexcept override {
		return input.GetAvailable(partial);
	}

	off_t _Skip(off_t length) noexcept override;

	void _Read() noexcept override {
		input.Read();
	}

	std::size_t _ConsumeBucketList(std::size_t nbytes) noexcept override {
		return Consumed(input.ConsumeBucketList(nbytes));
	}

	void _ConsumeDirect(std::size_t nbytes) noexcept override {
		input.ConsumeDirect(nbytes);
	}

	int _AsFd() noexcept override;

	/* virtual methods from class IstreamHandler */

	bool OnIstreamReady() noexcept override {
		return InvokeReady();
	}

	std::size_t OnData(std::span<const std::byte> src) noexcept override;

	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset,
				     std::size_t max_length) noexcept override;

	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "istream.hxx"
#include "Sink.hxx"

class FacadeIstream : public Istream, protected IstreamSink {
protected:
	template<typename I>
	FacadeIstream(struct pool &_pool, I &&_input TRACE_ARGS_DEFAULT) noexcept
		:Istream(_pool TRACE_ARGS_FWD), IstreamSink(std::forward<I>(_input)) {}

	explicit FacadeIstream(struct pool &_pool TRACE_ARGS_DEFAULT) noexcept
		:Istream(_pool TRACE_ARGS_FWD) {}

	template<typename I>
	void ReplaceInputDirect(I &&_input) noexcept {
		assert(input.IsDefined());

		input.Replace(std::forward<I>(_input), *this);
	}

	void FillBucketListFromInput(IstreamBucketList &list);
};

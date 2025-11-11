// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ForwardIstream.hxx"

/**
 * An #Istream filter that does not advertise the full length of the
 * underlying #Istream.
 */
class NoLengthIstream : public ForwardIstream {
public:
	template<typename I>
	NoLengthIstream(struct pool &_pool, I &&_input) noexcept
		:ForwardIstream(_pool, std::forward<I>(_input)) {}

protected:
	IstreamLength _GetLength() noexcept override {
		auto result = ForwardIstream::_GetLength();
		result.exhaustive = false;
		return result;
	}
};


// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Handler.hxx"
#include "Pointer.hxx"

/**
 * An #IstreamHandler implementation which manages a pointer to its
 * #Istream instance.
 */
class IstreamSink : protected IstreamHandler {
protected:
	IstreamPointer input;

	IstreamSink() noexcept = default;

	~IstreamSink() noexcept {
		if (HasInput())
			CloseInput();
	}

	template<typename I>
	explicit IstreamSink(I &&_input) noexcept
		:input(std::forward<I>(_input), *this) {}

	bool HasInput() const noexcept {
		return input.IsDefined();
	}

	template<typename I>
	void SetInput(I &&_input) noexcept  {
		input.Set(std::forward<I>(_input), *this);
	}

	void ClearInput() noexcept {
		input.Clear();
	}

	void CloseInput() noexcept {
		input.Close();
	}
};

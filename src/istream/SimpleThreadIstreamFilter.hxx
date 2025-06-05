// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ThreadIstream.hxx"

/**
 * A #ThreadIstreamFilter implementation that provides a simpler Run()
 * virtual method.
 */
class SimpleThreadIstreamFilter : public ThreadIstreamFilter {
	SliceFifoBuffer unprotected_input, unprotected_output;

public:
	// virtual methods from ThreadIstreamFilter
	void Run(ThreadIstreamInternal &i) final;
	void PostRun(ThreadIstreamInternal &i) noexcept override;

protected:
	struct Params {
		bool finish;
	};

	struct Result {
		bool drained;
	};

	virtual Result SimpleRun(SliceFifoBuffer &input, SliceFifoBuffer &output,
				 Params params) = 0;
};

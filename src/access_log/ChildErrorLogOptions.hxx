// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/TokenBucket.hxx"

/**
 * Run-time options for #ChildErrorLog.
 */
struct ChildErrorLogOptions {
	TokenBucketConfig rate_limit{.rate = -1, .burst = -1};

	bool is_default = true;
};

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

/**
 * Run-time options for #ChildErrorLog.
 */
struct ChildErrorLogOptions {
	double rate_limit = -1, burst = -1;

	bool is_default = true;
};

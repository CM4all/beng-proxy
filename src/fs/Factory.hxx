// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Ptr.hxx"
#include "util/LeakDetector.hxx"

class SocketFilterFactory : LeakDetector {
public:
	/**
	 * Throws std::runtime_error on error.
	 */
	virtual SocketFilterPtr CreateFilter() = 0;
};

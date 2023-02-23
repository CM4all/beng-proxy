// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Ptr.hxx"

class SocketFilterFactory {
public:
	/**
	 * Return an identifier for filters created by this factory.  This
	 * is used to match existing connections for reuse.
	 */
	virtual const char *GetFilterId() const = 0;

	/**
	 * Throws std::runtime_error on error.
	 */
	virtual SocketFilterPtr CreateFilter() = 0;
};

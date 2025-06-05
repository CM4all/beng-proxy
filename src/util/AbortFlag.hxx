// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/Cancellable.hxx"

/**
 * An #Cancellable implementation which sets a flag.  This can be
 * used by libraries which don't have their own implementation, but
 * need to know whether the operation has been aborted.
 */
class AbortFlag final : Cancellable {
public:
	bool aborted = false;

	explicit AbortFlag(CancellablePointer &cancel_ptr) {
		cancel_ptr = *this;
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		assert(!aborted);

		aborted = true;
	}
};

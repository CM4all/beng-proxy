// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/UniqueFileDescriptor.hxx"
#include "util/SharedLease.hxx"

/**
 * A simple wrapper for a file descriptor that can be used by multiple
 * entities.  The reference counter and leases are managed by
 * #SharedAnchor, i.e. instantiate a #SharedLease.  The file
 * descriptor will be closed as soon as the last lease is released,
 * which deletes this class.  Therefore, this class must be allocated
 * using the `new` operator and the memory is owned by all leases.
 */
class SharedFd final : public SharedAnchor {
	const UniqueFileDescriptor fd;

public:
	explicit SharedFd(UniqueFileDescriptor &&_fd) noexcept
		:fd(std::move(_fd)) {}

	FileDescriptor Get() const noexcept {
		return fd;
	}

protected:
	void OnAbandoned() noexcept override {
		delete this;
	}
};

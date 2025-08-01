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
 * which destructs this class, but does not free memory.  Therefore,
 * this class must be allocated by a #pool.
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
		Destroy();
	}

private:
	void Destroy() noexcept {
		this->~SharedFd();
	}
};

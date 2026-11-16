// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "system/linux/PidFD.h"
#include "io/UniqueFileDescriptor.hxx"

class AutoKillPidfd {
	UniqueFileDescriptor pidfd;

public:
	AutoKillPidfd(UniqueFileDescriptor &&_fd) noexcept
		:pidfd(std::move(_fd)) {}

	~AutoKillPidfd() noexcept {
		my_pidfd_send_signal(pidfd.Get(), SIGTERM, nullptr, 0);
	}
};

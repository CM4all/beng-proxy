// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SharedFd.hxx"

void
SharedFd::OnAbandoned() noexcept
{
	delete this;
}

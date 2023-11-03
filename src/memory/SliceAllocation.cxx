// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SliceAllocation.hxx"
#include "SliceArea.hxx"
#include "Checker.hxx"

void
SliceAllocation::Free() noexcept
{
	assert(IsDefined());

	if (HaveMemoryChecker())
		free(data);
	else
		area->Free(data);

	data = nullptr;
}

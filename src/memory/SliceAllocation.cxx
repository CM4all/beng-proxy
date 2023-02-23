// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SliceAllocation.hxx"
#include "SliceArea.hxx"

void
SliceAllocation::Free() noexcept
{
	assert(IsDefined());

	area->Free(data);
	data = nullptr;
}

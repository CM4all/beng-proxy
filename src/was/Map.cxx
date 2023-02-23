// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Map.hxx"
#include "was/async/Control.hxx"
#include "strmap.hxx"

namespace Was {

bool
SendMap(Control &control, enum was_command cmd,
	const StringMap &map) noexcept
{
	for (const auto &i : map)
		if (!control.SendPair(cmd, i.key, i.value))
			return false;

	return true;
}

} // namespace Was

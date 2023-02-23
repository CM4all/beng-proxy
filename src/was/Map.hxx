// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <was/protocol.h>

class StringMap;

namespace Was {

class Control;

bool
SendMap(Control &control, enum was_command cmd,
	const StringMap &map) noexcept;

} // namespace Was

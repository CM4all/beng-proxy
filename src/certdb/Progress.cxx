// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Progress.hxx"

#include <stdio.h>

void
WorkshopProgress::operator()(int _value) noexcept
{
	if (!IsEnabled())
		return;

	const unsigned value = Scale(Clamp(_value));
	if (use_control_channel)
		dprintf(3, "progress %u", value);
	else
		printf("%u\n", value);
}

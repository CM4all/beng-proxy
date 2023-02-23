// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Check.hxx"

#include <assert.h>
#include <sys/stat.h>

bool
LbHttpCheckConfig::Check() const noexcept
{
	assert(!file_exists.empty());

	struct stat st;
	return stat(file_exists.c_str(), &st) == 0;
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <sys/stat.h>
#include <sys/sysmacros.h>

static constexpr struct statx_timestamp
ToStatxTimestamp(const struct timespec &src) noexcept
{
	struct statx_timestamp dest{};
	dest.tv_sec = src.tv_sec;
	dest.tv_nsec = src.tv_nsec;
	return dest;
}

[[gnu::const]]
static struct statx
ToStatx(const struct stat &st) noexcept
{
	struct statx stx{};
	stx.stx_mask = STATX_TYPE|STATX_MODE|STATX_MTIME|STATX_INO|STATX_SIZE;
	stx.stx_mode = st.st_mode;
	stx.stx_ino = st.st_ino;
	stx.stx_size = st.st_size;
	stx.stx_mtime = ToStatxTimestamp(st.st_mtim);
	stx.stx_dev_major = major(st.st_dev);
	stx.stx_dev_minor = minor(st.st_dev);
	return stx;
}

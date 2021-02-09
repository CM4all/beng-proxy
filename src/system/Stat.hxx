/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

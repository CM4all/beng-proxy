// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Request.hxx"

#include <sys/stat.h>

struct Request::Handler::File::Precompressed {
	const char *compressed_path;

	const char *encoding;

	const struct statx original_st;

	UniqueFileDescriptor original_fd;

	enum Stat {
		AUTO_BROTLI,
		AUTO_GZIPPED,
		GZIPPED,
		END
	} state = AUTO_BROTLI;

	Precompressed(UniqueFileDescriptor &&_fd, const struct statx &_st) noexcept
		:original_st(_st), original_fd(std::move(_fd)) {}
};

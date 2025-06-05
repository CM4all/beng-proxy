// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Request.hxx"
#include "util/SharedLease.hxx"

#include <sys/stat.h>

struct Request::Handler::File::Precompressed {
	const char *compressed_path;

	std::string_view encoding;

	SharedLease original_lease;

	const struct statx original_st;

	FileDescriptor original_fd;

	enum Stat {
#ifdef HAVE_BROTLI
		AUTO_BROTLI,
#endif
		AUTO_GZIPPED,
		GZIPPED,
		END
	} state{};

	Precompressed(FileDescriptor _fd, const struct statx &_st, SharedLease &&_lease) noexcept
		:original_lease(std::move(_lease)), original_st(_st), original_fd(_fd) {}
};

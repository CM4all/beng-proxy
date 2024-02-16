// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string>

#include <sys/types.h> // for mode_t

class UniqueSocketDescriptor;

/**
 * Like #TempListener, but create the socket in a temporary directory
 * with a specific filename.
 */
class TempDirectoryListener {
	std::string directory;
	std::string socket;

	mode_t mode;

public:
	/**
	 * Throws on error.
	 */
	explicit TempDirectoryListener(mode_t _mode);

	~TempDirectoryListener() noexcept;

	constexpr TempDirectoryListener(TempDirectoryListener &&) noexcept = default;
	TempDirectoryListener &operator=(TempDirectoryListener &&) noexcept = default;

	const char *GetDirectory() const noexcept {
		return directory.c_str();
	}

	const char *GetSocketName() const noexcept {
		return socket.c_str();
	}

	/**
	 * Throws std::runtime_error on error.
	 */
	UniqueSocketDescriptor Create(std::string_view filename,
				      int socket_type, int backlog);
};

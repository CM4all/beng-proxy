// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TempDirectoryListener.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/SocketError.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"

#include <fmt/core.h>

#include <cassert>

#include <sys/stat.h> // for mkdir()
#include <unistd.h> // for unlink()

using std::string_view_literals::operator""sv;

static std::string
MakeTempDirectoryPathTemplate()
{
	const char *runtime_directory = getenv("RUNTIME_DIRECTORY");
	if (runtime_directory != nullptr)
		return fmt::format("{}/temp-socket-XXXXXX"sv, runtime_directory);
	else
		return std::string{"/tmp/cm4all-beng-proxy-socket-XXXXXX"sv};
}

static std::string
MakeTempDirectory()
{
	while (true) {
		auto path = MakeTempDirectoryPathTemplate();

		const char *p = mktemp(path.data());
		if (*p == 0)
			throw MakeErrno("mktemp() failed");

		if (mkdir(p, 0711) < 0) {
			const int e = errno;
			if (e == EEXIST)
				continue;

			throw MakeErrno(e, "Failed to create directory");
		}

		return path;
	}
}

TempDirectoryListener::TempDirectoryListener(mode_t _mode)
	:directory(MakeTempDirectory()),
	 mode(_mode)
{
}

TempDirectoryListener::~TempDirectoryListener() noexcept
{
	if (!directory.empty()) {
		if (!socket.empty())
			unlink(socket.c_str());

		rmdir(directory.c_str());
	}
}

UniqueSocketDescriptor
TempDirectoryListener::Create(std::string_view filename,
			      int socket_type, int backlog)
{
	assert(!directory.empty());
	assert(socket.empty());

	socket = fmt::format("{}/{}", directory, filename);

	AllocatedSocketAddress address;
	address.SetLocal(socket.c_str());

	UniqueSocketDescriptor fd;
	if (!fd.Create(AF_LOCAL, socket_type, 0))
		throw MakeSocketError("failed to create local socket");

	/* chmod() before bind() to prevent race conditions (if the
	   socket permissions are tighter than our umask) */
	fchmod(fd.Get(), mode);

	if (!fd.Bind(address))
		throw MakeSocketError("failed to bind local socket");

	/* chmod() again because bind() applies the umask to the mode
	   given to fchmod() above */
	chmod(socket.c_str(), mode);

	if (!fd.Listen(backlog))
		throw MakeSocketError("failed to listen on local socket");

	return fd;
}

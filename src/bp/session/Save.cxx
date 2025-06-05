// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Save.hxx"
#include "Write.hxx"
#include "Read.hxx"
#include "File.hxx"
#include "Manager.hxx"
#include "Session.hxx"
#include "io/BufferedOutputStream.hxx"
#include "io/BufferedReader.hxx"
#include "io/FdReader.hxx"
#include "io/FdOutputStream.hxx"
#include "io/FileWriter.hxx"
#include "io/Logger.hxx"

#include <assert.h>

static const char *session_save_path;

static void
session_save_callback(const Session *session, void *ctx)
{
	auto &file = *(BufferedOutputStream *)ctx;
	session_write_magic(file, MAGIC_SESSION);
	session_write(file, session);
}

static void
session_manager_save(SessionManager &manager, BufferedOutputStream &file)
{
	session_write_file_header(file);
	manager.Visit(session_save_callback, &file);
	session_write_file_tail(file);
}

inline bool
SessionManager::Load(BufferedReader &r)
{
	session_read_file_header(r);

	const Expiry now = Expiry::Now();

	unsigned num_added = 0, num_expired = 0;
	while (true) {
		uint32_t magic = session_read_magic(r);
		if (magic == MAGIC_END_OF_LIST)
			break;
		else if (magic != MAGIC_SESSION)
			return false;

		auto session = session_read(r, prng);
		assert(session);

		if (session->expires.IsExpired(now)) {
			/* this session is already expired, discard it
			   immediately */
			++num_expired;
			continue;
		}

		Insert(*session.release());
		++num_added;
	}

	LogConcat(4, "SessionManager",
		  "loaded ", num_added, " sessions, discarded ",
		  num_expired, " expired sessions");
	return true;
}

void
session_save(SessionManager &manager) noexcept
try {
	LogConcat(5, "SessionManager", "saving sessions to ", session_save_path);

	FileWriter fw(session_save_path, 0600);
	FdOutputStream fos(fw.GetFileDescriptor());

	WithBufferedOutputStream(fos, [&manager](BufferedOutputStream &bos){
		session_manager_save(manager, bos);
	});

	fw.Commit();
} catch (...) {
	LogConcat(2, "SessionManager", "Failed to save sessions",
		  std::current_exception());
	return;
}

void
session_save_init(SessionManager &manager, const char *path) noexcept
{
	assert(session_save_path == nullptr);

	if (path == nullptr)
		return;

	session_save_path = path;

	UniqueFileDescriptor fd;
	if (!fd.OpenReadOnly(session_save_path))
		return;

	try {
		FdReader fr(fd);
		BufferedReader br(fr);

		manager.Load(br);
	} catch (SessionDeserializerError) {
		LogConcat(1, "SessionManager",
			  "Session file is corrupt");
	} catch (...) {
		LogConcat(1, "SessionManager",
			  "Failed to load sessions: ",
			  std::current_exception());
	}
}

void
session_save_deinit(SessionManager &manager) noexcept
{
	if (session_save_path == nullptr)
		return;

	session_save(manager);
}

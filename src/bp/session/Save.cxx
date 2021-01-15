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

#include "Save.hxx"
#include "Write.hxx"
#include "Read.hxx"
#include "File.hxx"
#include "Manager.hxx"
#include "Session.hxx"
#include "io/BufferedOutputStream.hxx"
#include "io/FdOutputStream.hxx"
#include "io/FileWriter.hxx"
#include "io/Logger.hxx"

#include <assert.h>

static const char *session_save_path;

static bool
session_save_callback(const Session *session, void *ctx)
{
	auto &file = *(BufferedOutputStream *)ctx;
	session_write_magic(file, MAGIC_SESSION);
	session_write(file, session);
	return true;
}

static void
session_manager_save(SessionManager &manager, BufferedOutputStream &file)
{
	session_write_file_header(file);
	manager.Visit(session_save_callback, &file);
	session_write_file_tail(file);
}

static bool
session_manager_load(SessionManager &manager, FILE *file)
{
	if (!session_read_file_header(file))
		return false;

	const Expiry now = Expiry::Now();

	unsigned num_added = 0, num_expired = 0;
	while (true) {
		uint32_t magic = session_read_magic(file);
		if (magic == MAGIC_END_OF_LIST)
			break;
		else if (magic != MAGIC_SESSION)
			return false;

		auto session = session_read(file);
		if (session == nullptr)
			return false;

		if (session->expires.IsExpired(now)) {
			/* this session is already expired, discard it
			   immediately */
			++num_expired;
			continue;
		}

		manager.Insert(*session.release());
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

	FileWriter fw(session_save_path);
	FdOutputStream fos(fw.GetFileDescriptor());

	{
		BufferedOutputStream bos(fos);
		session_manager_save(manager, bos);
		bos.Flush();
	}

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

	FILE *file = fopen(session_save_path, "rb");
	if (file != nullptr) {
		session_manager_load(manager, file);
		fclose(file);
	}
}

void
session_save_deinit(SessionManager &manager) noexcept
{
	if (session_save_path == nullptr)
		return;

	session_save(manager);
}

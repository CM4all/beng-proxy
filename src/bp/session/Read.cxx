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

#include "Read.hxx"
#include "File.hxx"
#include "Session.hxx"

#include <stdint.h>

namespace {

class SessionDeserializerError {};

class FileReader {
	FILE *const file;

public:
	explicit FileReader(FILE *_file):file(_file) {}

	void ReadBuffer(void *buffer, size_t size) {
		if (fread(buffer, 1, size, file) != size)
			throw SessionDeserializerError();
	}

	template<typename T>
	void ReadT(T &value) {
		ReadBuffer(&value, sizeof(value));
	}

	template<typename T>
	T ReadT() {
		T value;
		ReadT<T>(value);
		return value;
	}

	uint8_t ReadByte() {
		return ReadT<uint8_t>();
	}

	bool ReadBool() {
		return ReadByte() != 0;
	}

	uint32_t Read32() {
		return ReadT<uint32_t>();
	}

	uint64_t Read64() {
		return ReadT<uint64_t>();
	}

	void Read(Expiry &value) {
		ReadT(value);
	}

	AllocatedString ReadString() {
		uint16_t length;
		ReadT(length);

		if (length == (uint16_t)-1)
			return nullptr;

		auto data = new char[length + 1];
		AllocatedString s = AllocatedString::Donate(data);
		ReadBuffer(data, length);
		data[length] = 0;
		return s;
	}

	AllocatedArray<std::byte> ReadArray() {
		uint16_t size;
		ReadT(size);

		if (size == (uint16_t)-1)
			return nullptr;

		AllocatedArray<std::byte> a(size);
		ReadBuffer(a.data(), size);
		return a;
	}
};

}

static void
Expect32(FileReader &file, uint32_t expected)
{
	if (file.Read32() != expected)
		throw SessionDeserializerError();
}

uint32_t
session_read_magic(FILE *file)
try {
	return FileReader(file).Read32();
} catch (SessionDeserializerError) {
	return 0;
}

bool
session_read_file_header(FILE *_file)
try {
	FileReader file(_file);
	Expect32(file, MAGIC_FILE);
	Expect32(file, sizeof(Session));
	return true;
} catch (SessionDeserializerError) {
	return false;
}

static void
ReadWidgetSessions(FileReader &file, WidgetSession::Set &widgets);

static void
DoReadWidgetSession(FileReader &file, WidgetSession &ws)
{
	ReadWidgetSessions(file, ws.children);
	ws.path_info = file.ReadString();
	ws.query_string = file.ReadString();
	Expect32(file, MAGIC_END_OF_RECORD);
}

static std::unique_ptr<WidgetSession>
ReadWidgetSession(FileReader &file)
{
	auto id = file.ReadString();
	auto ws = std::make_unique<WidgetSession>(std::move(id));
	DoReadWidgetSession(file, *ws);
	return ws;
}

static void
ReadWidgetSessions(FileReader &file, WidgetSession::Set &widgets)
{
	while (true) {
		uint32_t magic = file.Read32();
		if (magic == MAGIC_END_OF_LIST) {
			break;
		} else if (magic != MAGIC_WIDGET_SESSION)
			throw SessionDeserializerError();

		auto ws = ReadWidgetSession(file);
		if (widgets.insert(*ws).second)
			ws.release();
	}
}

static std::unique_ptr<Cookie>
ReadCookie(FileReader &file)
{
	auto name = file.ReadString();
	auto value = file.ReadString();

	auto cookie = std::make_unique<Cookie>(std::move(name),
					       std::move(value));
	cookie->domain = file.ReadString();
	cookie->path = file.ReadString();
	file.Read(cookie->expires);
	Expect32(file, MAGIC_END_OF_RECORD);
	return cookie;
}

static void
ReadCookieJar(FileReader &file, CookieJar &jar)
{
	while (true) {
		uint32_t magic = file.Read32();
		if (magic == MAGIC_END_OF_LIST)
			break;
		else if (magic != MAGIC_COOKIE)
			throw SessionDeserializerError();

		auto cookie = ReadCookie(file);
		jar.Add(*cookie.release());
	}
}

static std::unique_ptr<RealmSession>
ReadRealmSession(FileReader &file, Session &parent)
{
	auto name = file.ReadString();
	auto session = std::make_unique<RealmSession>(parent,
						      std::move(name));

	session->site = file.ReadString();
	session->user = file.ReadString();
	file.Read(session->user_expires);
	ReadWidgetSessions(file, session->widgets);
	ReadCookieJar(file, session->cookies);
	Expect32(file, MAGIC_END_OF_RECORD);

	return session;
}

static void
DoReadSession(FileReader &file, Session &session)
{
	file.Read(session.expires);
	file.ReadT(session.counter);
	session.is_new = file.ReadBool();
	session.cookie_sent = file.ReadBool();
	session.cookie_received = file.ReadBool();
	session.translate = file.ReadArray();
	session.language = file.ReadString();

	while (true) {
		uint32_t magic = file.Read32();
		if (magic == MAGIC_END_OF_LIST) {
			break;
		} else if (magic != MAGIC_REALM_SESSION)
			throw SessionDeserializerError();

		auto realm_session = ReadRealmSession(file, session);
		if (session.realms.insert(*realm_session).second)
			realm_session.release();
	}

	Expect32(file, MAGIC_END_OF_RECORD);
}

std::unique_ptr<Session>
session_read(FILE *_file)
try {
	FileReader file(_file);
	const auto id = file.ReadT<SessionId>();
	auto session = std::make_unique<Session>(id);
	DoReadSession(file, *session);
	return session;
} catch (SessionDeserializerError) {
	return nullptr;
}

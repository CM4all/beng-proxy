// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Write.hxx"
#include "File.hxx"
#include "Session.hxx"
#include "io/BufferedOutputStream.hxx"
#include "util/SpanCast.hxx"

#include <cstdint>
#include <stdexcept>

namespace {

class SessionSerializerError final : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

class FileWriter {
	BufferedOutputStream &os;

public:
	explicit FileWriter(BufferedOutputStream &_os) noexcept
		:os(_os) {}

	void WriteBuffer(std::span<const std::byte> src) {
		os.Write(src);
	}

	template<typename T>
	void WriteT(T &value) {
		os.WriteT<T>(value);
	}

	void WriteBool(const bool &value) {
		WriteT(value);
	}

	void Write16(const uint16_t &value) {
		WriteT(value);
	}

	void Write32(const uint32_t &value) {
		WriteT(value);
	}

	void Write64(const uint64_t &value) {
		WriteT(value);
	}

	void Write(const Expiry &value) {
		WriteT(value);
	}

	void Write(const char *s) {
		if (s == nullptr) {
			Write16(UINT16_MAX);
			return;
		}

		const std::string_view sv{s};
		if (sv.size() >= UINT16_MAX)
			throw SessionSerializerError("String is too long");

		Write16(sv.size());
		WriteBuffer(AsBytes(sv));
	}

	void Write(std::span<const std::byte> buffer) {
		if (buffer.data() == nullptr) {
			Write16(UINT16_MAX);
			return;
		}

		if (buffer.size() >= UINT16_MAX)
			throw SessionSerializerError("Buffer is too long");

		Write16(buffer.size());
		WriteBuffer(buffer);
	}

	void Write(std::string_view s) {
		Write(AsBytes(s));
	}
};

}

void
session_write_magic(BufferedOutputStream &os, uint32_t magic)
{
	FileWriter file(os);
	file.Write32(magic);
}

void
session_write_file_header(BufferedOutputStream &os)
{
	FileWriter file(os);
	file.Write32(MAGIC_FILE);
	file.Write32(sizeof(Session));
}

void
session_write_file_tail(BufferedOutputStream &file)
{
	session_write_magic(file, MAGIC_END_OF_LIST);
}

static void
WriteWidgetSessions(FileWriter &file, const WidgetSession::Set &widgets);

static void
WriteWidgetSession(FileWriter &file, const WidgetSession &session)
{
	WriteWidgetSessions(file, session.children);
	file.Write(session.path_info);
	file.Write(session.query_string);
	file.Write32(MAGIC_END_OF_RECORD);
}

static void
WriteWidgetSessions(FileWriter &file, const WidgetSession::Set &widgets)
{
	for (const auto &[id, ws] : widgets) {
		file.Write32(MAGIC_WIDGET_SESSION);
		file.Write(id);
		WriteWidgetSession(file, ws);
	}

	file.Write32(MAGIC_END_OF_LIST);
}

static void
WriteCookie(FileWriter &file, const Cookie &cookie)
{
	file.Write(cookie.name);
	file.Write(cookie.value);
	file.Write(cookie.domain);
	file.Write(cookie.path);
	file.Write(cookie.expires);
	file.Write32(MAGIC_END_OF_RECORD);
}

static void
WriteCookieJar(FileWriter &file, const CookieJar &jar)
{
	for (const auto &cookie : jar.cookies) {
		file.Write32(MAGIC_COOKIE);
		WriteCookie(file, cookie);
	}

	file.Write32(MAGIC_END_OF_LIST);
}

static void
WriteRealmSession(FileWriter &file, const RealmSession &session)
{
	file.Write(session.site);
	file.Write(session.translate);
	file.Write(session.user);
	file.Write(session.user_expires);
	WriteWidgetSessions(file, session.widgets);
	WriteCookieJar(file, session.cookies);
	file.WriteT(session.session_cookie_same_site);
	file.Write32(MAGIC_END_OF_RECORD);
}

void
session_write(BufferedOutputStream &os, const Session *session)
{
	FileWriter file(os);

	file.WriteT(session->id);
	file.Write(session->expires);
	file.WriteT(session->counter);
	file.WriteBool(session->cookie_received);
	file.Write(session->translate);
	file.Write(session->language);

	for (const auto &[name, realm] : session->realms) {
		file.Write32(MAGIC_REALM_SESSION);
		file.Write(name);
		WriteRealmSession(file, realm);
	}

	file.Write32(MAGIC_END_OF_LIST);
	file.Write32(MAGIC_END_OF_RECORD);
}

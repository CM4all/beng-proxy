// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Read.hxx"
#include "File.hxx"
#include "Session.hxx"
#include "io/BufferedReader.hxx"

namespace {

class FileReader {
	BufferedReader &r;

public:
	explicit FileReader(BufferedReader &_r) noexcept:r(_r) {}

	void ReadBuffer(void *buffer, size_t size) {
		r.ReadFull({static_cast<std::byte *>(buffer), size});
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
session_read_magic(BufferedReader &r)
{
	return FileReader(r).Read32();
}

void
session_read_file_header(BufferedReader &r)
{
	FileReader file(r);
	Expect32(file, MAGIC_FILE);
	Expect32(file, sizeof(Session));
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
ReadRealmSession(FileReader &file, uint32_t magic, Session &parent)
{
	bool have_translate;
	if (magic == MAGIC_REALM_SESSION) {
		/* since version 17.2 */
		have_translate = true;
	} else if (magic == MAGIC_REALM_SESSION_OLD) {
		/* until version 17.1 */
		have_translate = false;
	} else
		throw SessionDeserializerError();


	auto name = file.ReadString();
	auto session = std::make_unique<RealmSession>(parent,
						      std::move(name));

	session->site = file.ReadString();

	if (have_translate)
		session->translate = file.ReadArray();

	session->user = file.ReadString();
	file.Read(session->user_expires);
	ReadWidgetSessions(file, session->widgets);
	ReadCookieJar(file, session->cookies);
	session->session_cookie_same_site = file.ReadT<CookieSameSite>();
	Expect32(file, MAGIC_END_OF_RECORD);

	return session;
}

static void
DoReadSession(FileReader &file, Session &session)
{
	file.Read(session.expires);
	file.ReadT(session.counter);
	session.cookie_received = file.ReadBool();
	session.translate = file.ReadArray();
	session.language = file.ReadString();

	while (true) {
		uint32_t magic = file.Read32();
		if (magic == MAGIC_END_OF_LIST)
			break;

		auto realm_session = ReadRealmSession(file, magic, session);
		if (session.realms.insert(*realm_session).second)
			realm_session.release();
	}

	Expect32(file, MAGIC_END_OF_RECORD);
}

std::unique_ptr<Session>
session_read(BufferedReader &r, SessionPrng &prng)
{
	FileReader file(r);
	const auto id = file.ReadT<SessionId>();

	// TODO read salt from session file
	SessionId csrf_salt;
	csrf_salt.Generate(prng);

	auto session = std::make_unique<Session>(id, csrf_salt);
	DoReadSession(file, *session);
	return session;
}

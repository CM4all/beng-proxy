/*
 * Write a session to a file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_read.hxx"
#include "session_file.h"
#include "session.hxx"
#include "cookie_jar.hxx"
#include "shm/dpool.hxx"

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

    char *ReadString(struct dpool &pool) {
        uint16_t length;
        ReadT(length);

        if (length == (uint16_t)-1)
            return nullptr;

        char *s = (char *)d_malloc(&pool, length + 1);
        ReadBuffer(s, length);
        s[length] = 0;
        return s;
    }

    StringView ReadStringView(struct dpool &pool) {
        uint16_t length;
        ReadT(length);

        if (length == (uint16_t)-1)
            return nullptr;

        if (length == 0)
            return StringView::Empty();

        char *s = (char *)d_malloc(&pool, length);
        ReadBuffer(s, length);
        return {s, length};
    }

    ConstBuffer<void> ReadConstBuffer(struct dpool &pool) {
        uint16_t size;
        ReadT(size);

        if (size == (uint16_t)-1)
            return nullptr;

        if (size == 0)
            return { "", 0 };

        void *p = d_malloc(&pool, size);
        ReadBuffer(p, size);
        return { p, size };
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
ReadWidgetSessions(FileReader &file, Session &session,
                   WidgetSession *parent,
                   WidgetSession::Set &widgets)
    throw(std::bad_alloc, SessionDeserializerError);

static void
DoReadWidgetSession(FileReader &file, Session &session, WidgetSession &ws)
    throw(std::bad_alloc, SessionDeserializerError)
{
    struct dpool &pool = session.pool;

    ReadWidgetSessions(file, session, &ws, ws.children);
    ws.path_info = file.ReadString(pool);
    ws.query_string = file.ReadString(pool);
    Expect32(file, MAGIC_END_OF_RECORD);
}

static WidgetSession *
ReadWidgetSession(FileReader &file, Session &session, WidgetSession *parent)
    throw(std::bad_alloc, SessionDeserializerError)
{
    const char *id = file.ReadString(session.pool);
    auto *ws = NewFromPool<WidgetSession>(&session.pool, session, parent, id);
    DoReadWidgetSession(file, session, *ws);
    return ws;
}

static void
ReadWidgetSessions(FileReader &file, Session &session,
                   WidgetSession *parent,
                   WidgetSession::Set &widgets)
    throw(std::bad_alloc, SessionDeserializerError)
{
    while (true) {
        uint32_t magic = file.Read32();
        if (magic == MAGIC_END_OF_LIST) {
            break;
        } else if (magic != MAGIC_WIDGET_SESSION)
            throw SessionDeserializerError();

        auto *ws = ReadWidgetSession(file, session, parent);
        widgets.insert(*ws);
    }
}

static void
DoReadCookie(FileReader &file, struct dpool &pool, Cookie &cookie)
    throw(std::bad_alloc, SessionDeserializerError)
{
    cookie.name = file.ReadStringView(pool);
    cookie.value = file.ReadStringView(pool);
    cookie.domain = file.ReadString(pool);
    cookie.path = file.ReadString(pool);
    file.Read(cookie.expires);
    Expect32(file, MAGIC_END_OF_RECORD);
}

static Cookie *
ReadCookie(FileReader &file, struct dpool &pool)
    throw(std::bad_alloc, SessionDeserializerError)
{
    auto *cookie = NewFromPool<Cookie>(&pool, pool, nullptr, nullptr);
    DoReadCookie(file, pool, *cookie);
    return cookie;
}

static void
ReadCookieJar(FileReader &file, struct dpool &pool, CookieJar &jar)
    throw(std::bad_alloc, SessionDeserializerError)
{
    while (true) {
        uint32_t magic = file.Read32();
        if (magic == MAGIC_END_OF_LIST)
            break;
        else if (magic != MAGIC_COOKIE)
            throw SessionDeserializerError();

        auto *cookie = ReadCookie(file, pool);
        jar.Add(*cookie);
    }
}

static void
DoReadSession(FileReader &file, struct dpool &pool, Session &session)
    throw(std::bad_alloc)
{
    file.Read(session.expires);
    file.ReadT(session.counter);
    session.is_new = file.ReadBool();
    session.cookie_sent = file.ReadBool();
    session.cookie_received = file.ReadBool();
    session.realm = file.ReadString(pool);
    session.translate = file.ReadConstBuffer(pool);
    session.site = file.ReadString(pool);
    file.Read(session.user_expires);
    session.language = file.ReadString(pool);
    ReadWidgetSessions(file, session, nullptr, session.widgets);
    ReadCookieJar(file, pool, session.cookies);
    Expect32(file, MAGIC_END_OF_RECORD);
}

Session *
session_read(FILE *_file, struct dpool *pool)
    throw(std::bad_alloc)
try {
    FileReader file(_file);
    const auto id = file.ReadT<SessionId>();
    auto *session = NewFromPool<Session>(pool, *pool, id, nullptr);
    DoReadSession(file, *pool, *session);
    return session;
} catch (SessionDeserializerError) {
    return nullptr;
}

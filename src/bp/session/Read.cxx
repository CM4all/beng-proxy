/*
 * Copyright 2007-2017 Content Management AG
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

    DString ReadString(struct dpool &pool) {
        uint16_t length;
        ReadT(length);

        if (length == (uint16_t)-1)
            return nullptr;

        char *s = (char *)d_malloc(pool, length + 1);
        ReadBuffer(s, length);
        s[length] = 0;
        return DString::Donate(s);
    }

    StringView ReadStringView(struct dpool &pool) {
        uint16_t length;
        ReadT(length);

        if (length == (uint16_t)-1)
            return nullptr;

        if (length == 0)
            return "";

        char *s = (char *)d_malloc(pool, length);
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

        void *p = d_malloc(pool, size);
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
ReadWidgetSessions(FileReader &file, RealmSession &session,
                   WidgetSession::Set &widgets);

static void
DoReadWidgetSession(FileReader &file, RealmSession &session, WidgetSession &ws)
{
    struct dpool &pool = session.parent.pool;

    ReadWidgetSessions(file, session, ws.children);
    ws.path_info = file.ReadString(pool);
    ws.query_string = file.ReadString(pool);
    Expect32(file, MAGIC_END_OF_RECORD);
}

static WidgetSession *
ReadWidgetSession(FileReader &file, RealmSession &session)
{
    const char *id = file.ReadString(session.parent.pool);
    auto *ws = NewFromPool<WidgetSession>(session.parent.pool, session, id);
    DoReadWidgetSession(file, session, *ws);
    return ws;
}

static void
ReadWidgetSessions(FileReader &file, RealmSession &session,
                   WidgetSession::Set &widgets)
{
    while (true) {
        uint32_t magic = file.Read32();
        if (magic == MAGIC_END_OF_LIST) {
            break;
        } else if (magic != MAGIC_WIDGET_SESSION)
            throw SessionDeserializerError();

        auto *ws = ReadWidgetSession(file, session);
        widgets.insert(*ws);
    }
}

static void
DoReadCookie(FileReader &file, struct dpool &pool, Cookie &cookie)
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
{
    auto *cookie = NewFromPool<Cookie>(pool, pool, nullptr, nullptr);
    DoReadCookie(file, pool, *cookie);
    return cookie;
}

static void
ReadCookieJar(FileReader &file, struct dpool &pool, CookieJar &jar)
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
DoReadRealmSession(FileReader &file, struct dpool &pool, RealmSession &session)
{
    session.site = file.ReadString(pool);
    session.user = file.ReadString(pool);
    file.Read(session.user_expires);
    ReadWidgetSessions(file, session, session.widgets);
    ReadCookieJar(file, pool, session.cookies);
    Expect32(file, MAGIC_END_OF_RECORD);
}

static void
DoReadSession(FileReader &file, struct dpool &pool, Session &session)
{
    file.Read(session.expires);
    file.ReadT(session.counter);
    session.is_new = file.ReadBool();
    session.cookie_sent = file.ReadBool();
    session.cookie_received = file.ReadBool();
    session.translate = file.ReadConstBuffer(pool);
    session.language = file.ReadString(pool);

    while (true) {
        uint32_t magic = file.Read32();
        if (magic == MAGIC_END_OF_LIST) {
            break;
        } else if (magic != MAGIC_REALM_SESSION)
            throw SessionDeserializerError();

        const char *realm_name = file.ReadString(pool);
        auto *realm_session = NewFromPool<RealmSession>(pool, session,
                                                        realm_name);
        DoReadRealmSession(file, pool, *realm_session);
    }

    Expect32(file, MAGIC_END_OF_RECORD);
}

Session *
session_read(FILE *_file, struct dpool &pool)
try {
    FileReader file(_file);
    const auto id = file.ReadT<SessionId>();
    auto *session = NewFromPool<Session>(pool, pool, id);
    DoReadSession(file, pool, *session);
    return session;
} catch (SessionDeserializerError) {
    return nullptr;
}

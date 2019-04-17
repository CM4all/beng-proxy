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

#include "Write.hxx"
#include "File.hxx"
#include "Session.hxx"

#include <stdint.h>

namespace {

class SessionSerializerError {};

class FileWriter {
    FILE *const file;

public:
    explicit FileWriter(FILE *_file):file(_file) {}

    void WriteBuffer(const void *buffer, size_t size) {
        if (fwrite(buffer, 1, size, file) != size)
            throw SessionSerializerError();
    }

    template<typename T>
    void WriteT(T &value) {
        WriteBuffer(&value, sizeof(value));
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
            Write16((uint16_t)-1);
            return;
        }

        uint32_t length = strlen(s);
        if (length >= (uint16_t)-1)
            throw SessionSerializerError();

        Write16(length);
        WriteBuffer(s, length);
    }

    void Write(ConstBuffer<void> buffer) {
        if (buffer.IsNull()) {
            Write16((uint16_t)-1);
            return;
        }

        if (buffer.size >= (uint16_t)-1)
            throw SessionSerializerError();

        Write16(buffer.size);
        WriteBuffer(buffer.data, buffer.size);
    }

    void Write(StringView s) {
        Write(s.ToVoid());
    }

    void Write(const DString &s) {
        Write(s.ToStringView());
    }
};

}

bool
session_write_magic(FILE *_file, uint32_t magic)
try {
    FileWriter file(_file);
    file.Write32(magic);
    return true;
} catch (SessionSerializerError) {
    return false;
}

bool
session_write_file_header(FILE *_file)
try {
    FileWriter file(_file);

    file.Write32(MAGIC_FILE);
    file.Write32(sizeof(Session));
    return true;
} catch (SessionSerializerError) {
    return false;
}

bool
session_write_file_tail(FILE *file)
{
    return session_write_magic(file, MAGIC_END_OF_LIST);
}

static void
WriteWidgetSessions(FileWriter &file, const WidgetSession::Set &widgets);

static void
WriteWidgetSession(FileWriter &file, const WidgetSession &session)
{
    file.Write(session.id);
    WriteWidgetSessions(file, session.children);
    file.Write(session.path_info);
    file.Write(session.query_string);
    file.Write32(MAGIC_END_OF_RECORD);
}

static void
WriteWidgetSessions(FileWriter &file, const WidgetSession::Set &widgets)
{
    for (const auto &ws : widgets) {
        file.Write32(MAGIC_WIDGET_SESSION);
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
    file.Write(session.realm);
    file.Write(session.site);
    file.Write(session.user);
    file.Write(session.user_expires);
    WriteWidgetSessions(file, session.widgets);
    WriteCookieJar(file, session.cookies);
    file.Write32(MAGIC_END_OF_RECORD);
}

bool
session_write(FILE *_file, const Session *session)
try {
    FileWriter file(_file);

    file.WriteT(session->id);
    file.Write(session->expires);
    file.WriteT(session->counter);
    file.WriteBool(session->is_new);
    file.WriteBool(session->cookie_sent);
    file.WriteBool(session->cookie_received);
    file.Write(session->translate);
    file.Write(session->language);

    for (const auto &realm : session->realms) {
        file.Write32(MAGIC_REALM_SESSION);
        WriteRealmSession(file, realm);
    }

    file.Write32(MAGIC_END_OF_LIST);
    file.Write32(MAGIC_END_OF_RECORD);

    return true;
} catch (SessionSerializerError) {
    return false;
}

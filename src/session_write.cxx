/*
 * Write a session to a file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_write.hxx"
#include "session_file.h"
#include "session.hxx"
#include "cookie_jar.hxx"

#include <stdint.h>

static bool
write_8(FILE *file, uint8_t value)
{
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static bool
write_bool(FILE *file, bool value)
{
    return write_8(file, value);
}

static bool
write_16(FILE *file, uint16_t value)
{
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static bool
write_32(FILE *file, uint32_t value)
{
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static bool
write_64(FILE *file, uint64_t value)
{
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static bool
write_buffer(FILE *file, const void *data, size_t length)
{
    return fwrite(data, 1, length, file) == length;
}

static bool
write_session_id(FILE *file, const SessionId *id)
{
    return write_buffer(file, id, sizeof(*id));
}

static bool
write_string(FILE *file, const char *s)
{
    if (s == nullptr)
        return write_16(file, (uint16_t)-1);

    uint32_t length = strlen(s);
    if (length >= (uint16_t)-1)
        return false;

    return write_16(file, length) && write_buffer(file, s, length);
}

static bool
write_buffer(FILE *file, ConstBuffer<void> buffer)
{
    if (buffer.IsNull())
        return write_16(file, (uint16_t)-1);

    if (buffer.size >= (uint16_t)-1)
        return false;

    return write_16(file, buffer.size) &&
        write_buffer(file, buffer.data, buffer.size);
}

static bool
write_string(FILE *file, const StringView s)
{
    if (s.IsNull())
        return write_16(file, (uint16_t)-1);

    if (s.size >= (uint16_t)-1)
        return false;

    return write_16(file, s.size) &&
        write_buffer(file, s.data, s.size);
}

bool
session_write_magic(FILE *file, uint32_t magic)
{
    return write_32(file, magic);
}

bool
session_write_file_header(FILE *file)
{
    const Session *session = nullptr;

    return session_write_magic(file, MAGIC_FILE) &&
        write_32(file, sizeof(*session));
}

bool
session_write_file_tail(FILE *file)
{
    return session_write_magic(file, MAGIC_END_OF_LIST);
}

static bool
write_widget_sessions(FILE *file, const WidgetSession::Set &widgets);

static bool
write_widget_session(FILE *file, const WidgetSession *session)
{
    assert(session != nullptr);

    return write_string(file, session->id) &&
        write_widget_sessions(file, session->children) &&
        write_string(file, session->path_info) &&
        write_string(file, session->query_string) &&
        session_write_magic(file, MAGIC_END_OF_RECORD);
}

static bool
write_widget_sessions(FILE *file, const WidgetSession::Set &widgets)
{
    for (const auto &ws : widgets) {
        if (!session_write_magic(file, MAGIC_WIDGET_SESSION) ||
            !write_widget_session(file, &ws))
            return false;
    }

    return session_write_magic(file, MAGIC_END_OF_LIST);
}

static bool
write_cookie(FILE *file, const struct cookie *cookie)
{
    assert(cookie != nullptr);

    return write_string(file, cookie->name) &&
        write_string(file, cookie->value) &&
        write_string(file, cookie->domain) &&
        write_string(file, cookie->path) &&
        write_64(file, cookie->expires) &&
        session_write_magic(file, MAGIC_END_OF_RECORD);
}

static bool
write_cookie_jar(FILE *file, const struct cookie_jar *jar)
{
    for (const auto &cookie : jar->cookies)
        if (!session_write_magic(file, MAGIC_COOKIE) ||
            !write_cookie(file, &cookie))
            return false;

    return session_write_magic(file, MAGIC_END_OF_LIST);
}

bool
session_write(FILE *file, const Session *session)
{
    return write_session_id(file, &session->id) &&
        write_64(file, session->expires) &&
        write_32(file, session->counter) &&
        write_bool(file, session->is_new) &&
        write_bool(file, session->cookie_sent) &&
        write_bool(file, session->cookie_received) &&
        write_string(file, session->realm) &&
        write_buffer(file, session->translate) &&
        write_string(file, session->user) &&
        write_64(file, session->user_expires) &&
        write_string(file, session->language) &&
        write_widget_sessions(file, session->widgets) &&
        write_cookie_jar(file, session->cookies) &&
        session_write_magic(file, MAGIC_END_OF_RECORD);
}

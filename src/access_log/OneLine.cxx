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

#include "OneLine.hxx"
#include "Datagram.hxx"
#include "io/FileDescriptor.hxx"

#include <stdio.h>
#include <time.h>

static const char *
OptionalString(const char *p)
{
    if (p == nullptr)
        return "-";

    return p;
}

static bool
IsHarmlessChar(signed char ch)
{
    return ch >= 0x20 && ch != '"' && ch != '\\';
}

static const char *
EscapeString(const char *value, char *const buffer, size_t buffer_size)
{
    char *p = buffer, *const buffer_limit = buffer + buffer_size - 4;
    char ch;
    while (p < buffer_limit && (ch = *value++) != 0) {
        if (IsHarmlessChar(ch))
            *p++ = ch;
        else
            p += sprintf(p, "\\x%02X", (unsigned char)ch);
    }

    *p = 0;
    return buffer;
}

static const char *
EscapeString(StringView value, char *const buffer, size_t buffer_size)
{
    char *p = buffer, *const buffer_limit = buffer + buffer_size - 4;

    for (char ch : value) {
        if (p >= buffer_limit)
            break;

        if (IsHarmlessChar(ch))
            *p++ = ch;
        else
            p += sprintf(p, "\\x%02X", (unsigned char)ch);
    }

    *p = 0;
    return buffer;
}

static void
LogOneLineHttp(FileDescriptor fd, const AccessLogDatagram &d)
{
    const char *method = d.valid_http_method &&
        http_method_is_valid(d.http_method)
        ? http_method_to_string(d.http_method)
        : "?";

    char stamp_buffer[32];
    const char *stamp = "-";
    if (d.valid_timestamp) {
        time_t t = d.timestamp / 1000000;
        strftime(stamp_buffer, sizeof(stamp_buffer),
                 "%d/%b/%Y:%H:%M:%S %z", gmtime(&t));
        stamp = stamp_buffer;
    }

    char length_buffer[32];
    const char *length = "-";
    if (d.valid_length) {
        snprintf(length_buffer, sizeof(length_buffer), "%llu",
                 (unsigned long long)d.length);
        length = length_buffer;
    }

    char duration_buffer[32];
    const char *duration = "-";
    if (d.valid_duration) {
        snprintf(duration_buffer, sizeof(duration_buffer), "%llu",
                 (unsigned long long)d.duration);
        duration = duration_buffer;
    }

    char escaped_uri[4096], escaped_referer[2048], escaped_ua[1024];

    dprintf(fd.Get(),
            "%s %s - - [%s] \"%s %s HTTP/1.1\" %u %s \"%s\" \"%s\" %s\n",
            OptionalString(d.site),
            OptionalString(d.remote_host),
            stamp, method,
            EscapeString(d.http_uri, escaped_uri, sizeof(escaped_uri)),
            d.http_status, length,
            EscapeString(OptionalString(d.http_referer),
                         escaped_referer, sizeof(escaped_referer)),
            EscapeString(OptionalString(d.user_agent),
                         escaped_ua, sizeof(escaped_ua)),
            duration);
}

static void
LogOneLineMessage(FileDescriptor fd, const AccessLogDatagram &d)
{
    char stamp_buffer[32];
    const char *stamp = "-";
    if (d.valid_timestamp) {
        time_t t = d.timestamp / 1000000;
        strftime(stamp_buffer, sizeof(stamp_buffer),
                 "%d/%b/%Y:%H:%M:%S %z", gmtime(&t));
        stamp = stamp_buffer;
    }

    char escaped_message[4096];

    dprintf(fd.Get(),
            "%s [%s] %s\n",
            OptionalString(d.site),
            stamp,
            EscapeString(d.message, escaped_message,
                         sizeof(escaped_message)));
}

void
LogOneLine(FileDescriptor fd, const AccessLogDatagram &d)
{
    if (d.http_uri != nullptr && d.valid_http_status)
        LogOneLineHttp(fd, d);
    else if (!d.message.IsNull())
        LogOneLineMessage(fd, d);
}

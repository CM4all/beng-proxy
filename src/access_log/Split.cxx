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

/*
 * This logging server splits the log file into many, e.g. you may
 * have one log file per site.
 */

#include "Server.hxx"
#include "Datagram.hxx"
#include "util/ConstBuffer.hxx"

#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

static bool use_local_time = false;

static bool
string_equals(const char *a, size_t a_length, const char *b)
{
    size_t b_length = strlen(b);
    return a_length == b_length && memcmp(a, b, a_length) == 0;
}

static struct tm *
split_time_t(time_t t)
{
    return use_local_time ? localtime(&t) : gmtime(&t);
}

static const char *
expand_timestamp(const char *fmt, const AccessLogDatagram &d)
{
    if (!d.valid_timestamp)
        return nullptr;

    time_t t = (time_t)(d.timestamp / 1000000);
    static char buffer[64];
    strftime(buffer, sizeof(buffer), fmt, split_time_t(t));
    return buffer;
}

static const char *
expand(const char *name, size_t length, const AccessLogDatagram &d)
{
    if (string_equals(name, length, "site"))
        return d.site;
    else if (string_equals(name, length, "date"))
        return expand_timestamp("%Y-%m-%d", d);
    else if (string_equals(name, length, "year"))
        return expand_timestamp("%Y", d);
    else if (string_equals(name, length, "month"))
        return expand_timestamp("%m", d);
    else if (string_equals(name, length, "day"))
        return expand_timestamp("%d", d);
    else if (string_equals(name, length, "hour"))
        return expand_timestamp("%H", d);
    else if (string_equals(name, length, "minute"))
        return expand_timestamp("%M", d);
    else
        return nullptr;
}

static const char *
generate_path(const char *template_, const AccessLogDatagram &d)
{
    static char buffer[8192];
    char *dest = buffer;

    while (true) {
        const char *escape = strchr(template_, '%');
        if (escape == nullptr) {
            strcpy(dest, template_);
            return buffer;
        }

        if (dest + (escape - template_) + 1 >= buffer + sizeof(buffer))
            /* too long */
            return nullptr;

        memcpy(dest, template_, escape - template_);
        dest += escape - template_;
        template_ = escape + 1;
        if (*template_ != '{') {
            *dest++ = *escape;
            continue;
        }

        ++template_;
        const char *end = strchr(template_, '}');
        if (end == nullptr)
            return nullptr;

        const char *value = expand(template_, end - template_, d);
        if (value == nullptr)
            return nullptr;

        size_t length = strlen(value);
        if (dest + length >= buffer + sizeof(buffer))
            /* too long */
            return nullptr;

        memcpy(dest, value, length);
        dest += length;

        template_ = end + 1;
    }
}

static bool
make_parent_directory_recursive(char *path)
{
    char *slash = strrchr(path, '/');
    if (slash == nullptr || slash == path)
        return true;

    *slash = 0;
    int ret = mkdir(path, 0777);
    if (ret >= 0) {
        /* success */
        *slash = '/';
        return true;
    } else if (errno == ENOENT) {
        if (!make_parent_directory_recursive(path))
            return false;

        /* try again */
        ret = mkdir(path, 0777);
        *slash = '/';
        return ret >= 0;
    } else {
        fprintf(stderr, "Failed to create directory %s: %s\n",
                path, strerror(errno));

        return false;
    }
}

static bool
make_parent_directory(const char *path)
{
    char buffer[strlen(path)];
    strcpy(buffer, path);

    return make_parent_directory_recursive(buffer);
}

static int
open_log_file(const char *path)
{
    static int cache_fd = -1;
    static char cache_path[PATH_MAX];

    if (cache_fd >= 0) {
        if (strcmp(path, cache_path) == 0)
            return cache_fd;

        close(cache_fd);
        cache_fd = -1;
    }

    int fd = open(path, O_CREAT|O_APPEND|O_WRONLY|O_NOCTTY, 0666);
    if (fd < 0 && errno == ENOENT) {
        if (!make_parent_directory(path))
            return -1;

        /* try again */
        fd = open(path, O_CREAT|O_APPEND|O_WRONLY|O_NOCTTY, 0666);
    }

    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    cache_fd = fd;
    strcpy(cache_path, path);

    return fd;
}

static const char *
optional_string(const char *p)
{
    if (p == nullptr)
        return "-";

    return p;
}

static bool
harmless_char(signed char ch)
{
    return ch >= 0x20 && ch != '"' && ch != '\\';
}

static const char *
escape_string(const char *value, char *const buffer, size_t buffer_size)
{
    char *p = buffer, *const buffer_limit = buffer + buffer_size - 4;
    char ch;
    while (p < buffer_limit && (ch = *value++) != 0) {
        if (harmless_char(ch))
            *p++ = ch;
        else
            p += sprintf(p, "\\x%02X", (unsigned char)ch);
    }

    *p = 0;
    return buffer;
}

static void
dump_http(int fd, const AccessLogDatagram &d)
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
                 "%d/%b/%Y:%H:%M:%S %z", split_time_t(t));
        stamp = stamp_buffer;
    }

    char length_buffer[32];
    const char *length = "-";
    if (d.valid_length) {
        snprintf(length_buffer, sizeof(length_buffer), "%llu",
                 (unsigned long long)d.length);
        length = length_buffer;
    }

    char escaped_uri[4096], escaped_referer[2048], escaped_ua[1024];

    static char buffer[8192];
    snprintf(buffer, sizeof(buffer),
             "%s %s - - [%s] \"%s %s HTTP/1.1\" %u %s \"%s\" \"%s\"\n",
             optional_string(d.site),
             optional_string(d.remote_host),
             stamp, method,
             escape_string(d.http_uri, escaped_uri, sizeof(escaped_uri)),
             d.http_status, length,
             escape_string(optional_string(d.http_referer),
                           escaped_referer, sizeof(escaped_referer)),
             escape_string(optional_string(d.user_agent),
                           escaped_ua, sizeof(escaped_ua)));

    (void)write(fd, buffer, strlen(buffer));
}

static void
dump(int fd, const AccessLogDatagram &d)
{
    if (d.http_uri != nullptr && d.valid_http_status)
        dump_http(fd, d);
}

static bool
Dump(const char *template_path, const AccessLogDatagram &d)
{
    const char *path = generate_path(template_path, d);
    if (path == nullptr)
        return false;

    int fd = open_log_file(path);
    if (fd >= 0)
        dump(fd, d);

    return true;
}

int main(int argc, char **argv)
{
    int argi = 1;
    if (argi < argc && strcmp(argv[argi], "--localtime") == 0) {
        ++argi;
        use_local_time = true;
    }

    if (argi >= argc) {
        fprintf(stderr, "Usage: log-split [--localtime] TEMPLATE [...]\n");
        return EXIT_FAILURE;
    }

    ConstBuffer<const char *> templates(&argv[argi], argc - argi);

    AccessLogServer().Run([templates](const AccessLogDatagram &d){
            for (const char *t : templates)
                if (Dump(t, d))
                    break;
        });

    return 0;
}

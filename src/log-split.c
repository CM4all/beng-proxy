/*
 * This logging server splits the log file into many, e.g. you may
 * have one log file per site.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-server.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static bool
string_equals(const char *a, size_t a_length, const char *b)
{
    size_t b_length = strlen(b);
    return a_length == b_length && memcmp(a, b, a_length) == 0;
}

static const char *
expand_timestamp(const char *fmt, const struct log_datagram *d)
{
    if (!d->valid_timestamp)
        return NULL;

    time_t t = (time_t)(d->timestamp / 1000000);
    static char buffer[64];
    strftime(buffer, sizeof(buffer), fmt, gmtime(&t));
    return buffer;
}

static const char *
expand(const char *name, size_t length, const struct log_datagram *d)
{
    if (string_equals(name, length, "site"))
        return d->site;
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
        return NULL;
}

static const char *
generate_path(const char *template, const struct log_datagram *d)
{
    static char buffer[8192];
    char *dest = buffer;

    while (true) {
        const char *escape = strchr(template, '%');
        if (escape == NULL) {
            strcpy(dest, template);
            return buffer;
        }

        if (dest + (escape - template) + 1 >= buffer + sizeof(buffer))
            /* too long */
            return NULL;

        memcpy(dest, template, escape - template);
        dest += escape - template;
        template = escape + 1;
        if (*template != '{') {
            *dest++ = *escape;
            continue;
        }

        ++template;
        const char *end = strchr(template, '}');
        if (end == NULL)
            return NULL;

        const char *value = expand(template, end - template, d);
        if (value == NULL)
            return NULL;

        size_t length = strlen(value);
        if (dest + length >= buffer + sizeof(buffer))
            /* too long */
            return NULL;

        memcpy(dest, value, length);
        dest += length;

        template = end + 1;
    }
}

static bool
make_parent_directory_recursive(char *path)
{
    char *slash = strrchr(path, '/');
    if (slash == NULL || slash == path)
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

    return fd;
}

static void
dump_http(int fd, const struct log_datagram *d)
{
    const char *method = d->valid_http_method &&
        http_method_is_valid(d->http_method)
        ? http_method_to_string(d->http_method)
        : "?";

    const char *remote_host = d->remote_host;
    if (remote_host == NULL)
        remote_host = "-";

    const char *site = d->site;
    if (site == NULL)
        site = "-";

    char stamp_buffer[32];
    const char *stamp = "-";
    if (d->valid_timestamp) {
        time_t t = d->timestamp / 1000000;
        strftime(stamp_buffer, sizeof(stamp_buffer),
                 "%d/%b/%Y:%H:%M:%S %z", gmtime(&t));
        stamp = stamp_buffer;
    }

    char length_buffer[32];
    const char *length = "-";
    if (d->valid_length) {
        snprintf(length_buffer, sizeof(length_buffer), "%llu",
                 (unsigned long long)d->length);
        length = length_buffer;
    }

    static char buffer[8192];
    snprintf(buffer, sizeof(buffer),
             "%s %s - - [%s] \"%s %s HTTP/1.1\" %u %s\n",
             site, remote_host, stamp, method, d->http_uri,
             d->http_status, length);
    write(fd, buffer, strlen(buffer));
}

static void
dump(int fd, const struct log_datagram *d)
{
    if (d->http_uri != NULL && d->valid_http_status)
        dump_http(fd, d);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: log-split TEMPLATE [...]\n");
        return EXIT_FAILURE;
    }

    struct log_server *server = log_server_new(0);
    const struct log_datagram *d;
    while ((d = log_server_receive(server)) != NULL) {
        for (int i = 1; i < argc; ++i) {
            const char *path = generate_path(argv[i], d);
            if (path == NULL)
                continue;

            int fd = open_log_file(path);
            if (fd < 0)
                break;

            dump(fd, d);
            close(fd);
            break;
        }
    }

    return 0;
}

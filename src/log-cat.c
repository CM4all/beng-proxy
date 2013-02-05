/*
 * An example server for the logging protocol.  It prints the messages
 * to stdout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-server.h"

#include <stdio.h>
#include <time.h>

static const char *
optional_string(const char *p)
{
    if (p == NULL)
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
            p += sprintf(p, "\\x%02X", ch);
    }

    *p = 0;
    return buffer;
}

static void
dump_http(const struct log_datagram *d)
{
    const char *method = d->valid_http_method &&
        http_method_is_valid(d->http_method)
        ? http_method_to_string(d->http_method)
        : "?";

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

    char duration_buffer[32];
    const char *duration = "-";
    if (d->valid_duration) {
        snprintf(duration_buffer, sizeof(duration_buffer), "%llu",
                 (unsigned long long)d->duration);
        duration = duration_buffer;
    }

    char escaped_uri[4096], escaped_referer[2048], escaped_ua[1024];

    printf("%s %s - - [%s] \"%s %s HTTP/1.1\" %u %s \"%s\" \"%s\" %s\n",
           optional_string(d->site),
           optional_string(d->remote_host),
           stamp, method,
           escape_string(d->http_uri, escaped_uri, sizeof(escaped_uri)),
           d->http_status, length,
           escape_string(optional_string(d->http_referer),
                         escaped_referer, sizeof(escaped_referer)),
           escape_string(optional_string(d->user_agent),
                         escaped_ua, sizeof(escaped_ua)),
           duration);
}

static void
dump(const struct log_datagram *d)
{
    if (d->http_uri != NULL && d->valid_http_status)
        dump_http(d);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct log_server *server = log_server_new(0);
    const struct log_datagram *d;
    while ((d = log_server_receive(server)) != NULL)
        dump(d);

    return 0;
}

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

static void
dump_http(const struct log_datagram *d)
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

    printf("%s %s - - [%s] \"%s %s HTTP/1.1\" %u %s \"%s\" \"%s\"\n",
           site, remote_host, stamp, method, d->http_uri,
           d->http_status, length,
           optional_string(d->http_referer),
           optional_string(d->user_agent));
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

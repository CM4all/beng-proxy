/*
 * Print the site name and the bytes transferred for each request.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-server.h"

#include <stdio.h>

static void
dump(const struct log_datagram *d)
{
    if (d->site != NULL && d->valid_traffic)
        printf("%s %llu\n", d->site,
               (unsigned long long)(d->traffic_received + d->traffic_sent));
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

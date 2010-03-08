/*
 * Access logging.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "access-log.h"

#ifndef NO_ACCESS_LOG

#include "http-server.h"

#include <time.h>

void
access_log(struct http_server_request *request, const char *site,
           http_status_t status, off_t content_length)
{
    time_t now = time(NULL);
    char stamp[32];

    if (site == NULL)
        site = "-";

    strftime(stamp, sizeof(stamp),
             "%d/%b/%Y:%H:%M:%S %z", localtime(&now));

    if (content_length < 0)
        daemon_log(1, "%s %s - - [%s] \"%s %s HTTP/1.1\" %u %c\n",
                   site, request->remote_host, stamp,
                   http_method_to_string(request->method),
                   request->uri,
                   status,
                   content_length == -2 ? '-' : '?');
    else
        daemon_log(1, "%s %s - - [%s] \"%s %s HTTP/1.1\" %u %lu\n",
                   site, request->remote_host, stamp,
                   http_method_to_string(request->method),
                   request->uri,
                   status, (unsigned long)content_length);
}

#endif

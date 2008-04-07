/*
 * Access logging.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef NO_ACCESS_LOG

#include "access-log.h"
#include "http-server.h"

#include <time.h>

void
access_log(struct http_server_request *request,
           http_status_t status, istream_t body)
{
    off_t content_length = body == NULL ? -2 : istream_available(body, 0);
    time_t now = time(NULL);
    char stamp[32];

    strftime(stamp, sizeof(stamp),
             "%d/%b/%Y:%H:%M:%S %z", localtime(&now));

    if (content_length < 0)
        daemon_log(1, "%s - - [%s] \"%s %s HTTP/1.1\" %u %c\n",
                   request->remote_host, stamp,
                   http_method_to_string(request->method),
                   request->uri,
                   status,
                   content_length == -2 ? '-' : '?');
    else
        daemon_log(1, "%s - - [%s] \"%s %s HTTP/1.1\" %u %lu\n",
                   request->remote_host, stamp,
                   http_method_to_string(request->method),
                   request->uri,
                   status, content_length);
}

#endif

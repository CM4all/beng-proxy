/*
 * Access logging.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ACCESS_LOG_H
#define __BENG_ACCESS_LOG_H

#include "http.h"
#include "istream.h"

struct http_server_request;

#ifndef NO_ACCESS_LOG
#include <daemon/log.h>
#endif

#ifdef NO_ACCESS_LOG

static inline void
access_log(struct http_server_request *request __attr_unused,
           const char *site __attr_unused,
           http_status_t status __attr_unused, off_t length __attr_unused)
{
}

#else

void
access_log(struct http_server_request *request, const char *site,
           http_status_t status, off_t length);

#endif

#endif

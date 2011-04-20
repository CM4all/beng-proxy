/*
 * Access logging.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ACCESS_LOG_H
#define __BENG_ACCESS_LOG_H

#include "istream.h"

#include <http/status.h>

#include <stdint.h>

struct http_server_request;

#ifndef NO_ACCESS_LOG
#include <daemon/log.h>
#endif

#ifdef NO_ACCESS_LOG

static inline void
access_log(struct http_server_request *request __attr_unused,
           const char *site __attr_unused,
           const char *referer __attr_unused,
           const char *user_agent __attr_unused,
           http_status_t status __attr_unused, off_t length __attr_unused,
           uint64_t bytes_received __attr_unused,
           uint64_t bytes_sent __attr_unused)
{
}

#else

void
access_log(struct http_server_request *request, const char *site,
           const char *referer, const char *user_agent,
           http_status_t status, off_t length,
           uint64_t bytes_received, uint64_t bytes_sent);

#endif

#endif

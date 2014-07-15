/*
 * Access logging.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ACCESS_LOG_HXX
#define BENG_PROXY_ACCESS_LOG_HXX

#include <http/status.h>

#include <stdint.h>

struct http_server_request;

#ifndef NO_ACCESS_LOG
#include <daemon/log.h>
#endif

#ifdef NO_ACCESS_LOG

static inline void
access_log(gcc_unused struct http_server_request *request,
           gcc_unused const char *site,
           gcc_unused const char *referer,
           gcc_unused const char *user_agent,
           gcc_unused http_status_t status, gcc_unused off_t length,
           gcc_unused uint64_t bytes_received,
           gcc_unused uint64_t bytes_sent,
           gcc_unused uint64_t duration)
{
}

#else

void
access_log(struct http_server_request *request, const char *site,
           const char *referer, const char *user_agent,
           http_status_t status, off_t length,
           uint64_t bytes_received, uint64_t bytes_sent,
           uint64_t duration);

#endif

#endif

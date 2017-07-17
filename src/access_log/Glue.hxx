/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_GLUE_HXX
#define BENG_PROXY_LOG_GLUE_HXX

#include <http/method.h>
#include <http/status.h>

#include <chrono>

#include <stdint.h>

struct UidGid;
struct AccessLogDatagram;

void
log_global_init(const char *program, const UidGid *user);

void
log_global_deinit(void);

bool
log_global_enabled(void);

bool
log_http_request(const AccessLogDatagram &d);

#endif

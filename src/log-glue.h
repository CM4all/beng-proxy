/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_GLUE_H
#define BENG_PROXY_LOG_GLUE_H

#include <http/method.h>
#include <http/status.h>

#include <stdbool.h>
#include <stdint.h>

bool
log_global_init(const char *program);

void
log_global_deinit(void);

bool
log_global_enabled(void);

bool
log_http_request(uint64_t timestamp, http_method_t method, const char *uri,
                 const char *remote_host, const char *site,
                 const char *referer, const char *user_agent,
                 http_status_t status, uint64_t length);

#endif

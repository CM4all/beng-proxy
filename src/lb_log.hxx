/*
 * Logging with #lb_connection context.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_LOG_H
#define BENG_PROXY_LB_LOG_H

#include "glibfwd.hxx"

struct lb_connection;

void
lb_connection_log_error(int level, const struct lb_connection *connection,
                        const char *prefix, const char *error);

void
lb_connection_log_gerror(int level, const struct lb_connection *connection,
                         const char *prefix, GError *error);

void
lb_connection_log_errno(int level, const struct lb_connection *connection,
                        const char *prefix, int error);

#endif

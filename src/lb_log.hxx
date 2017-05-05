/*
 * Logging with #lb_connection context.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_LOG_H
#define BENG_PROXY_LB_LOG_H

#include "glibfwd.hxx"

#include <exception>

struct LbConnection;
struct LbHttpConnection;

void
lb_connection_log_error(int level, const LbConnection *connection,
                        const char *prefix, const char *error);

void
lb_connection_log_gerror(int level, const LbConnection *connection,
                         const char *prefix, GError *error);

void
lb_connection_log_error(int level, const LbConnection *connection,
                        const char *prefix, const std::exception &e);

void
lb_connection_log_error(int level, const LbConnection *connection,
                        const char *prefix, std::exception_ptr ep);

void
lb_connection_log_errno(int level, const LbConnection *connection,
                        const char *prefix, int error);

void
lb_connection_log_error(int level, const LbHttpConnection *connection,
                        const char *prefix, const char *error);

void
lb_connection_log_gerror(int level, const LbHttpConnection *connection,
                         const char *prefix, GError *error);

void
lb_connection_log_error(int level, const LbHttpConnection *connection,
                        const char *prefix, const std::exception &e);

void
lb_connection_log_error(int level, const LbHttpConnection *connection,
                        const char *prefix, std::exception_ptr ep);

#endif

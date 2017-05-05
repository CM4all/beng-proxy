/*
 * Logging with #lb_connection context.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_log.hxx"
#include "lb_connection.hxx"
#include "lb/HttpConnection.hxx"
#include "lb_config.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <string.h>

void
lb_connection_log_error(int level, const LbConnection *connection,
                        const char *prefix, const char *error)
{
    daemon_log(level, "%s (listener='%s' cluster='%s' client='%s'): %s\n",
               prefix,
               connection->listener.name.c_str(),
               connection->listener.destination.GetName(),
               connection->client_address,
               error);
}

void
lb_connection_log_gerror(int level, const LbConnection *connection,
                         const char *prefix, GError *error)
{
    lb_connection_log_error(level, connection, prefix, error->message);
}

void
lb_connection_log_error(int level, const LbConnection *connection,
                        const char *prefix, const std::exception &e)
{
    lb_connection_log_error(level, connection, prefix, e.what());

    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception &nested) {
        lb_connection_log_error(level, connection, prefix, nested);
    } catch (...) {
        lb_connection_log_error(level, connection, prefix,
                                "Unrecognized nested exception");
    }
}

void
lb_connection_log_error(int level, const LbConnection *connection,
                        const char *prefix, std::exception_ptr ep)
{
    try {
        std::rethrow_exception(ep);
    } catch (const std::exception &e) {
        lb_connection_log_error(level, connection, prefix, e);
    } catch (...) {
        lb_connection_log_error(level, connection, prefix,
                                "Unrecognized exception");
    }
}

void
lb_connection_log_error(int level, const LbHttpConnection *connection,
                        const char *prefix, const char *error)
{
    daemon_log(level, "%s (listener='%s' cluster='%s' client='%s'): %s\n",
               prefix,
               connection->listener.name.c_str(),
               connection->listener.destination.GetName(),
               connection->client_address,
               error);
}

void
lb_connection_log_gerror(int level, const LbHttpConnection *connection,
                         const char *prefix, GError *error)
{
    lb_connection_log_error(level, connection, prefix, error->message);
}

void
lb_connection_log_error(int level, const LbHttpConnection *connection,
                        const char *prefix, const std::exception &e)
{
    lb_connection_log_error(level, connection, prefix, e.what());

    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception &nested) {
        lb_connection_log_error(level, connection, prefix, nested);
    } catch (...) {
        lb_connection_log_error(level, connection, prefix,
                                "Unrecognized nested exception");
    }
}

void
lb_connection_log_error(int level, const LbHttpConnection *connection,
                        const char *prefix, std::exception_ptr ep)
{
    try {
        std::rethrow_exception(ep);
    } catch (const std::exception &e) {
        lb_connection_log_error(level, connection, prefix, e);
    } catch (...) {
        lb_connection_log_error(level, connection, prefix,
                                "Unrecognized exception");
    }
}

void
lb_connection_log_errno(int level, const LbConnection *connection,
                        const char *prefix, int error)
{
    lb_connection_log_error(level, connection, prefix, strerror(error));
}

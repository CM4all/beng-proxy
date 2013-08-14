/*
 * Logging with #lb_connection context.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_log.hxx"
#include "lb_connection.hxx"
#include "lb_config.hxx"

#include <daemon/log.h>

#include <string.h>

void
lb_connection_log_error(int level, const struct lb_connection *connection,
                        const char *prefix, const char *error)
{
    daemon_log(level, "%s (listener='%s' cluster='%s'): %s\n",
               prefix,
               connection->listener->name,
               connection->listener->cluster->name,
               error);
}

void
lb_connection_log_gerror(int level, const struct lb_connection *connection,
                         const char *prefix, GError *error)
{
    lb_connection_log_error(level, connection, prefix, error->message);
}

void
lb_connection_log_errno(int level, const struct lb_connection *connection,
                        const char *prefix, int error)
{
    lb_connection_log_error(level, connection, prefix, strerror(error));
}

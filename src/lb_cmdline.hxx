/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef LB_CMDLINE_HXX
#define LB_CMDLINE_HXX

#include "util/TrivialArray.hxx"

#include <daemon/user.h>

struct pool;

struct ListenerConfig {
    struct addrinfo *address;

    // TODO: free this string
    char *tag;
};

struct lb_cmdline {
    struct daemon_user user;

    /**
     * The configuration file.
     */
    const char *config_path;

    const char *access_logger;

    /**
     * The Bulldog data path.
     */
    const char *bulldog_path;

    unsigned tcp_stock_limit;

    bool watchdog;

    /**
     * If true, then the environment (e.g. the configuration file) is
     * checked, and the process exits.
     */
    bool check;
};

void
parse_cmdline(struct lb_cmdline *config, struct pool *pool,
              int argc, char **argv);

#endif

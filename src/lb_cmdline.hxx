/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef LB_CMDLINE_HXX
#define LB_CMDLINE_HXX

#include <daemon/user.h>

struct LbCmdLine {
    struct daemon_user user, logger_user;

    /**
     * The configuration file.
     */
    const char *config_path = "/etc/cm4all/beng/lb.conf";

    const char *access_logger = nullptr;

    /**
     * The Bulldog data path.
     */
    const char *bulldog_path = nullptr;

    unsigned tcp_stock_limit = 256;

    bool watchdog = false;

    /**
     * If true, then the environment (e.g. the configuration file) is
     * checked, and the process exits.
     */
    bool check = false;

    LbCmdLine();
};

void
ParseCommandLine(LbCmdLine *config,
                 int argc, char **argv);

#endif

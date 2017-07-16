/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef LB_CMDLINE_HXX
#define LB_CMDLINE_HXX

#include "spawn/UidGid.hxx"

struct LbConfig;

struct LbCmdLine {
    UidGid user;

    UidGid logger_user;

    /**
     * The configuration file.
     */
    const char *config_path = "/etc/cm4all/beng/lb.conf";

    /**
     * The Bulldog data path.
     */
    const char *bulldog_path = nullptr;

    unsigned tcp_stock_limit = 256;

    /**
     * If true, then the environment (e.g. the configuration file) is
     * checked, and the process exits.
     */
    bool check = false;
};

void
ParseCommandLine(LbCmdLine &cmdline, LbConfig &config,
                 int argc, char **argv);

#endif

/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CMDLINE_HXX
#define BENG_PROXY_CMDLINE_HXX

#include <daemon/user.h>

struct BpConfig;

#ifdef NDEBUG
static const bool debug_mode = false;
#else
extern bool debug_mode;
#endif

struct BpCmdLine {
    struct daemon_user user, logger_user;

    const char *access_logger = nullptr;

    BpCmdLine();
};

void
parse_cmdline(BpCmdLine &cmdline, BpConfig &config, int argc, char **argv);

#endif

/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CMDLINE_HXX
#define BENG_PROXY_CMDLINE_HXX

#include "spawn/UidGid.hxx"

struct BpConfig;

#ifdef NDEBUG
static const bool debug_mode = false;
#else
extern bool debug_mode;
#endif

struct BpCmdLine {
    UidGid user;

    UidGid logger_user;

    const char *config_file = "/etc/cm4all/beng/proxy/beng-proxy.conf";
};

void
ParseCommandLine(BpCmdLine &cmdline, BpConfig &config, int argc, char **argv);

#endif

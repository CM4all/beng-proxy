/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CMDLINE_HXX
#define BENG_PROXY_CMDLINE_HXX

struct BpConfig;

void
parse_cmdline(BpConfig &config, int argc, char **argv);

#endif

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Parse command line options.
 */

#pragma once

#include "spawn/UidGid.hxx"

struct BpConfig;

#ifdef NDEBUG
static const bool debug_mode = false;
#else
extern bool debug_mode;
#endif

struct BpCmdLine {
	UidGid logger_user;

	const char *config_file = "/etc/cm4all/beng/proxy/beng-proxy.conf";

	const char *debug_listener_tag = nullptr;
};

void
ParseCommandLine(BpCmdLine &cmdline, BpConfig &config, int argc, char **argv);

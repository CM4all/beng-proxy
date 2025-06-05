// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "spawn/UidGid.hxx"

struct LbConfig;

struct LbCmdLine {
	UidGid logger_user;

	/**
	 * The configuration file.
	 */
	const char *config_path = "/etc/cm4all/beng/lb.conf";

	/**
	 * If true, then the environment (e.g. the configuration file) is
	 * checked, and the process exits.
	 */
	bool check = false;
};

/**
 * Parse command line options.
 */
void
ParseCommandLine(LbCmdLine &cmdline, LbConfig &config,
		 int argc, char **argv);

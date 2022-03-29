/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "CommandLine.hxx"
#include "Config.hxx"
#include "io/Logger.hxx"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

static void
PrintUsage()
{
	puts("usage: cm4all-beng-lb [options]\n\n"
	     "valid options:\n"
	     " -h             help (this text)\n"
#ifdef __GLIBC__
	     " --version\n"
#endif
	     " -V             show cm4all-beng-lb version\n"
#ifdef __GLIBC__
	     " --verbose\n"
#endif
	     " -v             be more verbose\n"
#ifdef __GLIBC__
	     " --quiet\n"
#endif
	     " -q             be quiet\n"
#ifdef __GLIBC__
	     " --config-file PATH\n"
#endif
	     " -f PATH        load this configuration file instead of /etc/cm4all/beng/lb.conf\n"
#ifdef __GLIBC__
	     " --check        check configuration file syntax\n"
#else
	     " -C             check configuration file syntax\n"
#endif
#ifdef __GLIBC__
	     " --logger-user name\n"
#endif
	     " -U name        execute the access logger program with this user id\n"
#ifdef __GLIBC__
	     " --set NAME=VALUE  tweak an internal variable, see manual for details\n"
#endif
	     " -s NAME=VALUE  \n"
	     "\n"
	     );
}

static void arg_error(const char *argv0, const char *fmt, ...)
	__attribute__ ((noreturn))
	__attribute__((format(printf,2,3)));
static void arg_error(const char *argv0, const char *fmt, ...) {
	if (fmt != NULL) {
		va_list ap;

		fputs(argv0, stderr);
		fputs(": ", stderr);

		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);

		putc('\n', stderr);
	}

	fprintf(stderr, "Try '%s --help' for more information.\n",
		argv0);
	exit(1);
}

static void
HandleSet(LbConfig &config, const char *argv0, const char *p)
{
	const char *eq;

	eq = strchr(p, '=');
	if (eq == NULL)
		arg_error(argv0, "No '=' found in --set argument");

	if (eq == p)
		arg_error(argv0, "No name found in --set argument");

	const StringView name(p, eq - p);
	const char *const value = eq + 1;

	try {
		config.HandleSet(name, value);
	} catch (const std::runtime_error &e) {
		arg_error(argv0, "Error while parsing \"--set %.*s\": %s",
			  (int)name.size, name.data, e.what());
	}
}

/** read configuration options from the command line */
void
ParseCommandLine(LbCmdLine &cmdline, LbConfig &config,
		 int argc, char **argv)
{
	int ret;
#ifdef __GLIBC__
	static const struct option long_options[] = {
		{"help", 0, NULL, 'h'},
		{"version", 0, NULL, 'V'},
		{"verbose", 0, NULL, 'v'},
		{"quiet", 0, NULL, 'q'},
		{"config-file", 1, NULL, 'f'},
		{"check", 0, NULL, 'C'},
		{"user", 1, NULL, 'u'},
		{"logger-user", 1, NULL, 'U'},
		{"set", 1, NULL, 's'},
		{NULL,0,NULL,0}
	};
#endif
	unsigned verbose = 1;

	while (1) {
#ifdef __GLIBC__
		int option_index = 0;

		ret = getopt_long(argc, argv, "hVvqf:CU:B:s:",
				  long_options, &option_index);
#else
		ret = getopt(argc, argv, "hVvqf:CU:B:s:");
#endif
		if (ret == -1)
			break;

		switch (ret) {
		case 'h':
			PrintUsage();
			exit(0);

		case 'V':
			printf("cm4all-beng-lb v%s\n", VERSION);
			exit(0);

		case 'v':
			++verbose;
			break;

		case 'q':
			verbose = 0;
			break;

		case 'f':
			cmdline.config_path = optarg;
			break;

		case 'C':
			cmdline.check = true;
			break;

		case 'U':
			cmdline.logger_user.Lookup(optarg);
			break;

		case 's':
			HandleSet(config, argv[0], optarg);
			break;

		case '?':
			arg_error(argv[0], NULL);

		default:
			exit(1);
		}
	}

	SetLogLevel(verbose);

	/* check non-option arguments */

	if (optind < argc)
		arg_error(argv[0], "unrecognized argument: %s", argv[optind]);
}

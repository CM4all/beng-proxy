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
#include "net/Parser.hxx"
#include "io/Logger.hxx"
#include "util/StringView.hxx"
#include "util/IterableSplitString.hxx"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>

static void
PrintUsage()
{
	puts("usage: cm4all-beng-proxy [options]\n\n"
	     "valid options:\n"
#ifdef __GLIBC__
	     " --help\n"
#endif
	     " -h             help (this text)\n"
#ifdef __GLIBC__
	     " --version\n"
#endif
	     " -V             show cm4all-beng-proxy version\n"
#ifdef __GLIBC__
	     " --verbose\n"
#endif
	     " -v             be more verbose\n"
#ifdef __GLIBC__
	     " --quiet\n"
#endif
	     " -q             be quiet\n"
#ifdef __GLIBC__
	     " --config-file file\n"
#endif
	     " -f file        load this configuration file\n"
#ifdef __GLIBC__
	     " --logger-user name\n"
#endif
	     " -U name        execute the error logger program with this user id\n"
#ifdef __GLIBC__
	     " --document-root DIR\n"
#endif
	     " -r DIR         set the document root\n"
#ifdef __GLIBC__
	     " --translation-socket PATH\n"
#endif
	     " -t PATH        set the path to the translation server socket\n"
#ifdef __GLIBC__
	     " --cluster-size N\n"
#endif
	     " -C N           set the size of the beng-lb cluster\n"
#ifdef __GLIBC__
	     " --cluster-node N\n"
#endif
	     " -N N           set the index of this node in the beng-lb cluster\n"
#ifdef __GLIBC__
	     " --ua-classes PATH\n"
#endif
	     " -a PATH        load the User-Agent classification rules from this file\n"
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
	if (fmt != nullptr) {
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

template<typename F>
static void
SplitForEach(const char *p, char separator, F &&f)
{
	for (auto value : IterableSplitString(p, separator))
		if (!value.empty())
			f(std::string(value.data, value.size).c_str());
}

static void
HandleSet(BpConfig &config,
	  const char *argv0, const char *p)
{
	const char *eq;

	eq = strchr(p, '=');
	if (eq == nullptr)
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
ParseCommandLine(BpCmdLine &cmdline, BpConfig &config, int argc, char **argv)
{
	int ret;
	char *endptr;
#ifdef __GLIBC__
	static constexpr struct option long_options[] = {
		{"help", 0, nullptr, 'h'},
		{"version", 0, nullptr, 'V'},
		{"verbose", 0, nullptr, 'v'},
		{"quiet", 0, nullptr, 'q'},
		{"config-file", 1, nullptr, 'f'},
		{"user", 1, nullptr, 'u'},
		{"logger-user", 1, nullptr, 'U'},
		{"translation-socket", 1, nullptr, 't'},
		{"cluster-size", 1, nullptr, 'C'},
		{"cluster-node", 1, nullptr, 'N'},
		{"ua-classes", 1, nullptr, 'a'},
		{"set", 1, nullptr, 's'},
		{"debug-listener-tag", 1, nullptr, 'L'},
		{nullptr, 0, nullptr, 0}
	};
#endif
	unsigned verbose = 1;

	while (1) {
#ifdef __GLIBC__
		int option_index = 0;

		ret = getopt_long(argc, argv,
				  "hVvqf:U:t:B:C:N:s:",
				  long_options, &option_index);
#else
		ret = getopt(argc, argv,
			     "hVvqf:U:t:B:C:N:s:");
#endif
		if (ret == -1)
			break;

		switch (ret) {
		case 'h':
			PrintUsage();
			exit(0);

		case 'V':
			printf("cm4all-beng-proxy v%s\n", VERSION);
			exit(0);

		case 'v':
			++verbose;
			break;

		case 'q':
			verbose = 0;
			break;

		case 'f':
			cmdline.config_file = optarg;
			break;

		case 'U':
			cmdline.logger_user.Lookup(optarg);
			break;

		case 't':
			config.translation_sockets.emplace_front(ParseSocketAddress(optarg, 0, false));
			break;

		case 'C':
			config.cluster_size = strtoul(optarg, &endptr, 10);
			if (endptr == optarg || *endptr != 0 ||
			    config.cluster_size > 1024)
				arg_error(argv[0], "Invalid cluster size number");

			if (config.cluster_node >= config.cluster_size)
				config.cluster_node = 0;
			break;

		case 'N':
			config.cluster_node = strtoul(optarg, &endptr, 10);
			if (endptr == optarg || *endptr != 0)
				arg_error(argv[0], "Invalid cluster size number");

			if ((config.cluster_node != 0 || config.cluster_size != 0) &&
			    config.cluster_node >= config.cluster_size)
				arg_error(argv[0], "Cluster node too large");
			break;

		case 'a':
			cmdline.ua_classification_file = optarg;
			break;

		case 's':
			HandleSet(config, argv[0], optarg);
			break;

		case 'L':
			cmdline.debug_listener_tag = optarg;
			break;

		case '?':
			arg_error(argv[0], nullptr);

		default:
			exit(1);
		}
	}

	SetLogLevel(verbose);

	/* check non-option arguments */

	if (optind < argc)
		arg_error(argv[0], "unrecognized argument: %s", argv[optind]);
}

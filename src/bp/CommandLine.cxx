/*
 * Copyright 2007-2020 CM4all GmbH
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
#include "net/AddressInfo.hxx"
#include "net/Resolver.hxx"
#include "net/Parser.hxx"
#include "pool/pool.hxx"
#include "ua_classification.hxx"
#include "io/Logger.hxx"
#include "util/StringView.hxx"
#include "util/IterableSplitString.hxx"

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>

enum Options {
	START = 0x100,
	ALLOW_USER,
	ALLOW_GROUP,
	SPAWN_USER,
};

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
	     " --user name\n"
#endif
	     " -u name        switch to another user id\n"
	     " --allow-user NAME,NAME,...\n"
	     "                allow spawning child processes as the given users\n"
	     " --allow-group NAME,NAME,...\n"
	     "                allow spawning child processes as the given groups\n"
	     " --spawn-user USER[:GROUP]\n"
	     "                spawn child processes as this user/group by default\n"
#ifdef __GLIBC__
	     " --logger-user name\n"
#endif
	     " -U name        execute the error logger program with this user id\n"
#ifdef __GLIBC__
	     " --port PORT\n"
#endif
	     " -p PORT        the TCP port beng-proxy listens on\n"
#ifdef __GLIBC__
	     " --listen [TAG=]IP:PORT\n"
#endif
	     " -L IP:PORT     listen on this IP address\n"
#ifdef __GLIBC__
	     " --control-listen IP:PORT\n"
#endif
	     " -c IP:PORT     listen on this UDP port for control commands\n"
#ifdef __GLIBC__
	     " --multicast-group IP\n"
#endif
	     " -m IP          join this multicast group\n"
#ifdef __GLIBC__
	     " --workers COUNT\n"
#endif
	     " -w COUNT       set the number of worker processes; 0=don't fork\n"
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
ParseListenerConfig(const char *s,
		    std::forward_list<BpConfig::Listener> &list)
{
	std::string tag;

	const char *equals = strrchr(s, '=');
	if (equals != nullptr) {
		tag.assign(s, equals);
		s = equals + 1;
	}

	if (*s == '/' || *s == '@') {
		AllocatedSocketAddress address;
		address.SetLocal(s);
		list.emplace_front(std::move(address), tag);
		return;
	}

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_ADDRCONFIG|AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;

	for (const auto &i : Resolve(s, debug_mode ? 8080 : 80, &hints))
		list.emplace_front(i, tag);
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
ParseAllowUser(SpawnConfig &config, const char *arg)
{
	SplitForEach(arg, ',', [&config](const char *name){
		char *endptr;
		unsigned long i = strtoul(name, &endptr, 10);
		if (endptr > name && *endptr == 0) {
			config.allowed_uids.insert(i);
			return;
		}

		struct passwd *pw = getpwnam(name);
		if (pw == nullptr) {
			fprintf(stderr, "No such user: %s\n", name);
			exit(EXIT_FAILURE);
		}

		config.allowed_uids.insert(pw->pw_uid);
	});
}

static void
ParseAllowGroup(SpawnConfig &config, const char *arg)
{
	SplitForEach(arg, ',', [&config](const char *name){
		char *endptr;
		unsigned long i = strtoul(name, &endptr, 10);
		if (endptr > name && *endptr == 0) {
			config.allowed_gids.insert(i);
			return;
		}

		struct group *gr = getgrnam(name);
		if (gr == nullptr) {
			fprintf(stderr, "No such group: %s\n", name);
			exit(EXIT_FAILURE);
		}

		config.allowed_gids.insert(gr->gr_gid);
	});
}

static void
HandleSet(BpConfig &config,
	  const char *argv0, const char *p)
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
ParseCommandLine(BpCmdLine &cmdline, BpConfig &config, int argc, char **argv)
{
	int ret;
	char *endptr;
#ifdef __GLIBC__
	static const struct option long_options[] = {
		{"help", 0, NULL, 'h'},
		{"version", 0, NULL, 'V'},
		{"verbose", 0, NULL, 'v'},
		{"quiet", 0, NULL, 'q'},
		{"access-logger", 1, NULL, 'A'},
		{"config-file", 1, nullptr, 'f'},
		{"user", 1, NULL, 'u'},
		{"logger-user", 1, NULL, 'U'},
		{"allow-user", 1, NULL, ALLOW_USER},
		{"allow-group", 1, NULL, ALLOW_GROUP},
		{"spawn-user", 1, nullptr, SPAWN_USER},
		{"port", 1, NULL, 'p'},
		{"listen", 1, NULL, 'L'},
		{"control-listen", 1, NULL, 'c'},
		{"multicast-group", 1, NULL, 'm'},
		{"workers", 1, NULL, 'w'},
		{"document-root", 1, NULL, 'r'},
		{"translation-socket", 1, NULL, 't'},
		{"cluster-size", 1, NULL, 'C'},
		{"cluster-node", 1, NULL, 'N'},
		{"ua-classes", 1, NULL, 'a'},
		{"set", 1, NULL, 's'},
		{NULL,0,NULL,0}
	};
#endif
	const char *user_name = NULL;
	const char *spawn_user = nullptr;
	unsigned verbose = 1;

	while (1) {
#ifdef __GLIBC__
		int option_index = 0;

		ret = getopt_long(argc, argv,
				  "hVvqA:f:u:U:p:L:c:m:w:r:t:B:C:N:s:",
				  long_options, &option_index);
#else
		ret = getopt(argc, argv,
			     "hVvqA:f:u:U:p:L:c:m:w:r:t:B:C:N:s:");
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

		case 'A':
			config.access_log.SetLegacy(optarg);
			break;

		case 'f':
			cmdline.config_file = optarg;
			break;

		case 'u':
			if (debug_mode)
				arg_error(argv[0], "cannot specify a user in debug mode");

			user_name = optarg;
			break;

		case ALLOW_USER:
			ParseAllowUser(config.spawn, optarg);
			break;

		case ALLOW_GROUP:
			ParseAllowGroup(config.spawn, optarg);
			break;

		case SPAWN_USER:
			if (*optarg != 0)
				spawn_user = optarg;
			break;

		case 'U':
			if (debug_mode)
				arg_error(argv[0], "cannot specify a user in debug mode");

			cmdline.logger_user.Lookup(optarg);
			break;

		case 'p':
			if (config.ports.full())
				arg_error(argv[0], "too many listener ports");
			ret = (unsigned)strtoul(optarg, &endptr, 10);
			if (*endptr != 0)
				arg_error(argv[0], "invalid number after --port");
			if (ret <= 0 || ret > 0xffff)
				arg_error(argv[0], "invalid port after --port");
			config.ports.push_back(ret);
			break;

		case 'L':
			ParseListenerConfig(optarg, config.listen);
			break;

		case 'c':
			config.control_listen.emplace_front(ParseSocketAddress(optarg, 5478, true));
			break;

		case 'm':
			config.multicast_group = ParseSocketAddress(optarg, 0, false);
			break;

		case 'w':
			config.num_workers = (unsigned)strtoul(optarg, &endptr, 10);
			if (*endptr != 0)
				arg_error(argv[0], "invalid number after --workers");
			if (config.num_workers > 1024)
				arg_error(argv[0], "too many workers configured");

#ifdef HAVE_LIBSYSTEMD
			if (config.num_workers == 1 && sd_booted())
				/* we don't need a watchdog process if systemd watches
				   on us */
				config.num_workers = 0;
#endif

			break;

		case 'r':
			config.document_root = optarg;
			break;

		case 't':
			config.translation_socket = ParseSocketAddress(optarg, 0, false);
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

	/* copy the multicast_group to all command-line control listeners
	   (for backwards compatibility) */

	if (!config.multicast_group.IsNull())
		for (auto &i : config.control_listen)
			i.multicast_group = config.multicast_group;

	/* check completeness */

	if (user_name != NULL) {
		cmdline.user.Lookup(user_name);
		if (!cmdline.user.IsComplete())
			arg_error(argv[0], "refusing to run as root");
	} else if (!debug_mode)
		arg_error(argv[0], "no user name specified (-u)");

	if (debug_mode) {
		if (spawn_user != nullptr)
			arg_error(argv[0], "cannot set --spawn-user in debug mode");

		config.spawn.default_uid_gid.LoadEffective();
	} else if (spawn_user != nullptr) {
		auto &u = config.spawn.default_uid_gid;
		u.Lookup(spawn_user);
		if (!u.IsComplete())
			arg_error(argv[0], "refusing to spawn child processes as root");

		config.spawn.allowed_uids.insert(u.uid);
		config.spawn.allowed_gids.insert(u.gid);
		for (size_t i = 0; i < u.groups.size() && u.groups[i] != 0; ++i)
			config.spawn.allowed_gids.insert(u.groups[i]);
	} else {
		config.spawn.default_uid_gid = cmdline.user;
	}

	assert(config.spawn.default_uid_gid.IsComplete());
}

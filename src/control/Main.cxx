// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "net/control/Client.hxx"
#include "translation/Protocol.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "util/ByteOrder.hxx"
#include "util/ConstBuffer.hxx"
#include "util/PrintException.hxx"
#include "util/SpanCast.hxx"
#include "util/StringCompare.hxx"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

struct Usage {
	const char *msg = nullptr;
};

static void
SimpleCommand(const char *server, ConstBuffer<const char *> args,
	      BengProxy::ControlCommand cmd)
{
	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControlClient client(server);
	client.Send(cmd);
}

static void
Nop(const char *server, ConstBuffer<const char *> args)
{
	SimpleCommand(server, args, BengProxy::ControlCommand::NOP);
}

static constexpr struct {
	const char *name;
	TranslationCommand cmd;
} tcache_invalidate_strings[] = {
	{ "URI", TranslationCommand::URI },
	{ "PARAM", TranslationCommand::PARAM },
	{ "LISTENER_TAG", TranslationCommand::LISTENER_TAG },
	{ "REMOTE_HOST", TranslationCommand::REMOTE_HOST },
	{ "HOST", TranslationCommand::HOST },
	{ "LANGUAGE", TranslationCommand::LANGUAGE },
	{ "USER_AGENT", TranslationCommand::USER_AGENT },
	{ "QUERY_STRING", TranslationCommand::QUERY_STRING },
	{ "SITE", TranslationCommand::SITE },
	{ "INTERNAL_REDIRECT", TranslationCommand::INTERNAL_REDIRECT },
	{ "ENOTDIR", TranslationCommand::ENOTDIR_ },
	{ "USER", TranslationCommand::USER },
};

static std::string
ParseTcacheInvalidate(std::string_view name, const char *value)
{
	for (const auto &i : tcache_invalidate_strings)
		if (name == i.name)
			return BengControlClient::MakeTcacheInvalidate(i.cmd, value);

	throw FmtRuntimeError("Unrecognized key: '{}'", name);
}

static std::string
ParseTcacheInvalidate(const char *s)
{
	const char *eq = strchr(s, '=');
	if (eq == nullptr)
		throw FmtRuntimeError("Missing '=': {}", s);

	if (eq == s)
		throw FmtRuntimeError("Missing name: {}", s);

	return ParseTcacheInvalidate({s, eq}, eq + 1);
}

static void
TcacheInvalidate(const char *server, ConstBuffer<const char *> args)
{
	std::string payload;

	for (const char *s : args)
		payload += ParseTcacheInvalidate(s);

	BengControlClient client(server);
	client.Send(BengProxy::ControlCommand::TCACHE_INVALIDATE, payload);
}

static void
Verbose(const char *server, ConstBuffer<const char *> args)
{
	if (args.empty())
		throw Usage{"Log level missing"};

	const char *s = args.shift();

	if (!args.empty())
		throw Usage{"Too many arguments"};

	uint8_t log_level = atoi(s);

	BengControlClient client(server);
	client.Send(BengProxy::ControlCommand::VERBOSE,
		    ReferenceAsBytes(log_level));
}

static void
EnableNode(const char *server, ConstBuffer<const char *> args)
{
	if (args.empty())
		throw Usage{"Node name missing"};

	const std::string_view name = args.shift();

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControlClient client(server);
	client.Send(BengProxy::ControlCommand::ENABLE_NODE, name);
}

static void
FadeNode(const char *server, ConstBuffer<const char *> args)
{
	if (args.empty())
		throw Usage{"Node name missing"};

	const std::string_view name = args.shift();

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControlClient client(server);
	client.Send(BengProxy::ControlCommand::FADE_NODE, name);
}

static void
NodeStatus(const char *server, ConstBuffer<const char *> args)
{
	if (args.empty())
		throw Usage{"Node name missing"};

	const std::string_view name = args.shift();

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControlClient client(server);
	client.AutoBind();
	client.Send(BengProxy::ControlCommand::NODE_STATUS, name);

	const auto response = client.Receive();
	if (response.first != BengProxy::ControlCommand::NODE_STATUS)
		throw std::runtime_error("Wrong response command");

	const auto nul = response.second.find('\0');
	if (nul == response.second.npos)
		throw std::runtime_error("Malformed response payload");

	printf("%s\n", response.second.c_str() + nul + 1);
}

static void
PrintStatsAttribute(const char *name, const uint32_t &value) noexcept
{
	if (value != 0)
		printf("%s %" PRIu32 "\n", name, FromBE32(value));
}

static void
PrintStatsAttribute(const char *name, const uint64_t &value) noexcept
{
	if (value != 0)
		printf("%s %" PRIu64 "\n", name, FromBE64(value));
}

static void
Stats(const char *server, ConstBuffer<const char *> args)
{
	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControlClient client(server);
	client.AutoBind();
	client.Send(BengProxy::ControlCommand::STATS);

	const auto response = client.Receive();
	if (response.first != BengProxy::ControlCommand::STATS)
		throw std::runtime_error("Wrong response command");

	BengProxy::ControlStats stats;
	memset(&stats, 0, sizeof(stats));
	memcpy(&stats, response.second.data(),
	       std::min(sizeof(stats), response.second.size()));

	PrintStatsAttribute("incoming_connections", stats.incoming_connections);
	PrintStatsAttribute("outgoing_connections", stats.outgoing_connections);
	PrintStatsAttribute("children", stats.children);
	PrintStatsAttribute("sessions", stats.sessions);
	PrintStatsAttribute("http_requests", stats.http_requests);
	PrintStatsAttribute("translation_cache_size", stats.translation_cache_size);
	PrintStatsAttribute("http_cache_size", stats.http_cache_size);
	PrintStatsAttribute("filter_cache_size", stats.filter_cache_size);
	PrintStatsAttribute("translation_cache_brutto_size", stats.translation_cache_brutto_size);
	PrintStatsAttribute("http_cache_brutto_size", stats.http_cache_brutto_size);
	PrintStatsAttribute("filter_cache_brutto_size", stats.filter_cache_brutto_size);
	PrintStatsAttribute("nfs_cache_size", stats.nfs_cache_size);
	PrintStatsAttribute("nfs_cache_brutto_size", stats.nfs_cache_brutto_size);
	PrintStatsAttribute("io_buffers_size", stats.io_buffers_size);
	PrintStatsAttribute("io_buffers_brutto_size", stats.io_buffers_brutto_size);
	PrintStatsAttribute("http_traffic_received", stats.http_traffic_received);
	PrintStatsAttribute("http_traffic_sent", stats.http_traffic_sent);
}

static void
FadeChildren(const char *server, ConstBuffer<const char *> args)
{
	std::string_view tag{};

	if (!args.empty())
		tag = args.shift();

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControlClient client(server);
	client.Send(BengProxy::ControlCommand::FADE_CHILDREN, tag);
}

static void
FlushFilterCache(const char *server, ConstBuffer<const char *> args)
{
	std::string_view tag{};

	if (!args.empty())
		tag = args.shift();

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControlClient client(server);
	client.Send(BengProxy::ControlCommand::FLUSH_FILTER_CACHE, tag);
}

static void
DiscardSession(const char *server, ConstBuffer<const char *> args)
{
	if (args.empty())
		throw Usage{"Not enough arguments"};

	const std::string_view attach_id = args.shift();

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControlClient client(server);
	client.Send(BengProxy::ControlCommand::DISCARD_SESSION, attach_id);
}

static void
Stopwatch(const char *server, ConstBuffer<const char *> args)
{
	if (!args.empty())
		throw Usage{"Too many arguments"};

	UniqueFileDescriptor r, w;
	if (!UniqueFileDescriptor::CreatePipe(r, w))
		throw MakeErrno("pipe() failed");

	FileDescriptor fds[] = { w };

	BengControlClient client(server);
	client.Send(BengProxy::ControlCommand::STOPWATCH_PIPE, nullptr, fds);

	w.Close();

	while (true) {
		char buffer[8192];
		ssize_t nbytes = r.Read(buffer, sizeof(buffer));
		if (nbytes <= 0)
			break;

		if (write(STDOUT_FILENO, buffer, nbytes) < 0)
			break;
	}
}

int
main(int argc, char **argv)
try {
	ConstBuffer<const char *> args(argv + 1, argc - 1);

	const char *server = "@bp-control";

	while (!args.empty() && args.front()[0] == '-') {
		const char *option = args.shift();
		if (const char *new_server = StringAfterPrefix(option, "--server=")) {
			server = new_server;
		} else
			throw Usage{"Unknown option"};
	}

	if (args.empty())
		throw Usage();

	const char *const command = args.shift();

	if (StringIsEqual(command, "nop")) {
		Nop(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "tcache-invalidate")) {
		TcacheInvalidate(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "enable-node")) {
		EnableNode(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "fade-node")) {
		FadeNode(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "node-status")) {
		NodeStatus(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "dump-pools")) {
		SimpleCommand(server, args,
			      BengProxy::ControlCommand::DUMP_POOLS);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "stats")) {
		Stats(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "verbose")) {
		Verbose(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "fade-children")) {
		FadeChildren(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "disable-zeroconf")) {
		SimpleCommand(server, args,
			      BengProxy::ControlCommand::DISABLE_ZEROCONF);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "enable-zeroconf")) {
		SimpleCommand(server, args,
			      BengProxy::ControlCommand::ENABLE_ZEROCONF);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "flush-nfs-cache")) {
		SimpleCommand(server, args, BengProxy::ControlCommand::FLUSH_NFS_CACHE);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "flush-filter-cache")) {
		FlushFilterCache(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "discard-session")) {
		DiscardSession(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "stopwatch")) {
		Stopwatch(server, args);
		return EXIT_SUCCESS;
	} else
		throw Usage{"Unknown command"};
} catch (const Usage &u) {
	if (u.msg)
		fprintf(stderr, "%s\n\n", u.msg);

	fprintf(stderr, "Usage: %s [--server=SERVER[:PORT]] COMMAND ...\n"
		"\n"
		"Commands:\n"
		"  nop\n"
		"  tcache-invalidate [KEY=VALUE...]\n"
		"  enable-node NAME:PORT\n"
		"  fade-node NAME:PORT\n"
		"  dump-pools\n"
		"  verbose LEVEL\n"
		"  fade-children [TAG]\n"
		"  disable-zeroconf\n"
		"  enable-zeroconf\n"
		"  flush-nfs-cache\n"
		"  flush-filter-cache [TAG]\n"
		"  discard-session ATTACH_ID\n"
		"  stopwatch\n"
		"\n"
		"Names for tcache-invalidate:\n",
		argv[0]);

	for (const auto &i : tcache_invalidate_strings)
		fprintf(stderr, "  %s\n", i.name);

	return EXIT_FAILURE;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}

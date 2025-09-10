// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "net/control/Client.hxx"
#include "translation/Protocol.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "io/Pipe.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "util/ByteOrder.hxx"
#include "util/PackedBigEndian.hxx"
#include "util/PrintException.hxx"
#include "util/SpanCast.hxx"
#include "util/StringCompare.hxx"

#include <chrono>
#include <span>

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

struct Usage {
	const char *msg = nullptr;
};

static void
SimpleCommand(const char *server, std::span<const char *const> args,
	      BengControl::Command cmd)
{
	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(cmd);
}

static void
Nop(const char *server, std::span<const char *const> args)
{
	SimpleCommand(server, args, BengControl::Command::NOP);
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
	{ "CACHE_TAG", TranslationCommand::CACHE_TAG },
	{ "INTERNAL_REDIRECT", TranslationCommand::INTERNAL_REDIRECT },
	{ "ENOTDIR", TranslationCommand::ENOTDIR_ },
	{ "USER", TranslationCommand::USER },
};

static std::string
ParseTcacheInvalidate(std::string_view name, const char *value)
{
	for (const auto &i : tcache_invalidate_strings)
		if (name == i.name)
			return BengControl::Client::MakeTcacheInvalidate(i.cmd, value);

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
TcacheInvalidate(const char *server, std::span<const char *const> args)
{
	std::string payload;

	for (const char *s : args)
		payload += ParseTcacheInvalidate(s);

	BengControl::Client client(server);
	client.Send(BengControl::Command::TCACHE_INVALIDATE, payload);
}

static void
Verbose(const char *server, std::span<const char *const> args)
{
	if (args.empty())
		throw Usage{"Log level missing"};

	const char *s = args.front();
	args = args.subspan(1);

	if (!args.empty())
		throw Usage{"Too many arguments"};

	uint8_t log_level = atoi(s);

	BengControl::Client client(server);
	client.Send(BengControl::Command::VERBOSE,
		    ReferenceAsBytes(log_level));
}

static void
EnableNode(const char *server, std::span<const char *const> args)
{
	if (args.empty())
		throw Usage{"Node name missing"};

	const std::string_view name = args.front();
	args = args.subspan(1);

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(BengControl::Command::ENABLE_NODE, name);
}

static void
FadeNode(const char *server, std::span<const char *const> args)
{
	if (args.empty())
		throw Usage{"Node name missing"};

	const std::string_view name = args.front();
	args = args.subspan(1);

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(BengControl::Command::FADE_NODE, name);
}

static void
FadeChildren(const char *server, std::span<const char *const> args)
{
	std::string_view tag{};

	if (!args.empty()) {
		tag = args.front();
		args = args.subspan(1);
	}

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(BengControl::Command::FADE_CHILDREN, tag);
}

static void
TerminateChildren(const char *server, std::span<const char *const> args)
{
	std::string_view tag{};

	if (!args.empty()) {
		tag = args.front();
		args = args.subspan(1);
	}

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(BengControl::Command::TERMINATE_CHILDREN, tag);
}

static void
DisconnectDatabase(const char *server, std::span<const char *const> args)
{
	if (args.empty())
		throw Usage{"Not enough arguments"};

	const std::string_view tag = args.front();
	args = args.subspan(1);

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(BengControl::Command::DISCONNECT_DATABASE, tag);
}

static void
DisableUring(const char *server, std::chrono::duration<uint_least32_t> seconds)
{
	const PackedBE32 payload{seconds.count()};
	BengControl::Client client(server);
	client.Send(BengControl::Command::DISABLE_URING, ReferenceAsBytes(payload));
}

static void
DisableUring(const char *server)
{
	BengControl::Client client(server);
	client.Send(BengControl::Command::DISABLE_URING);
}

static void
FlushHttpCache(const char *server, std::span<const char *const> args)
{
	std::string_view tag{};

	if (!args.empty()) {
		tag = args.front();
		args = args.subspan(1);
	}

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(BengControl::Command::FLUSH_HTTP_CACHE, tag);
}

static void
FlushFilterCache(const char *server, std::span<const char *const> args)
{
	std::string_view tag{};

	if (!args.empty()) {
		tag = args.front();
		args = args.subspan(1);
	}

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(BengControl::Command::FLUSH_FILTER_CACHE, tag);
}

static void
DiscardSession(const char *server, std::span<const char *const> args)
{
	if (args.empty())
		throw Usage{"Not enough arguments"};

	const std::string_view attach_id = args.front();
	args = args.subspan(1);

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(BengControl::Command::DISCARD_SESSION, attach_id);
}

static void
ResetLimiter(const char *server, std::span<const char *const> args)
{
	if (args.empty())
		throw Usage{"Not enough arguments"};

	const std::string_view id = args.front();
	args = args.subspan(1);

	if (!args.empty())
		throw Usage{"Too many arguments"};

	BengControl::Client client(server);
	client.Send(BengControl::Command::RESET_LIMITER, id);
}

static void
Stopwatch(const char *server, std::span<const char *const> args)
{
	if (!args.empty())
		throw Usage{"Too many arguments"};

	auto [r, w] = CreatePipe();

	FileDescriptor fds[] = { w };

	BengControl::Client client(server);
	client.Send(BengControl::Command::STOPWATCH_PIPE, nullptr, fds);

	w.Close();

	while (true) {
		std::byte buffer[8192];
		ssize_t nbytes = r.Read(buffer);
		if (nbytes <= 0)
			break;

		if (write(STDOUT_FILENO, buffer, nbytes) < 0)
			break;
	}
}

int
main(int argc, char **argv)
try {
	std::span<const char *const> args{argv + 1, static_cast<std::size_t>(argc - 1)};

	const char *server = "@bp-control";

	while (!args.empty() && args.front()[0] == '-') {
		const char *option = args.front();
		args = args.subspan(1);
		if (const char *new_server = StringAfterPrefix(option, "--server=")) {
			server = new_server;
		} else
			throw Usage{"Unknown option"};
	}

	if (args.empty())
		throw Usage();

	const char *const command = args.front();
	args = args.subspan(1);

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
	} else if (StringIsEqual(command, "verbose")) {
		Verbose(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "reload-state")) {
		SimpleCommand(server, args,
			      BengControl::Command::RELOAD_STATE);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "fade-children")) {
		FadeChildren(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "terminate-children")) {
		TerminateChildren(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "disconnect-database")) {
		DisconnectDatabase(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "disable-uring")) {
		if (args.empty()) {
			DisableUring(server);
			return EXIT_SUCCESS;
		} else if (args.size() == 1) {
			DisableUring(server, std::chrono::duration<uint_least32_t>{atoi(args.front())});
			return EXIT_SUCCESS;
		} else
			throw Usage{"Too many arguments"};
	} else if (StringIsEqual(command, "enable-uring")) {
		if (!args.empty())
			throw Usage{"Too many arguments"};

		DisableUring(server, std::chrono::duration<uint_least32_t>::zero());
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "disable-zeroconf")) {
		SimpleCommand(server, args,
			      BengControl::Command::DISABLE_ZEROCONF);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "enable-zeroconf")) {
		SimpleCommand(server, args,
			      BengControl::Command::ENABLE_ZEROCONF);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "flush-http-cache")) {
		FlushHttpCache(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "flush-filter-cache")) {
		FlushFilterCache(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "discard-session")) {
		DiscardSession(server, args);
		return EXIT_SUCCESS;
	} else if (StringIsEqual(command, "reset-limiter")) {
		ResetLimiter(server, args);
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
		"  verbose LEVEL\n"
		"  reload-state\n"
		"  fade-children [TAG]\n"
		"  terminate-children TAG\n"
		"  disconnect-database TAG\n"
		"  disable-uring [SECONDS]\n"
		"  enable-uring\n"
		"  disable-zeroconf\n"
		"  enable-zeroconf\n"
		"  flush-http-cache [TAG]\n"
		"  flush-filter-cache [TAG]\n"
		"  discard-session ATTACH_ID\n"
		"  reset-limiter ID\n"
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

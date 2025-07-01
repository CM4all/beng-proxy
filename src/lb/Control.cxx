// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Control.hxx"
#include "Instance.hxx"
#include "Config.hxx"
#include "pool/tpool.hxx"
#include "pool/pool.hxx"
#include "translation/InvalidateParser.hxx"
#include "net/FormatAddress.hxx"
#include "net/FailureManager.hxx"
#include "net/FailureRef.hxx"
#include "util/Exception.hxx"
#include "util/SpanCast.hxx"
#include "util/StringSplit.hxx"
#include "AllocatorPtr.hxx"

#ifdef HAVE_AVAHI
#include "lib/avahi/Publisher.hxx"
#endif

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-journal.h>
#endif

#include <string.h>
#include <stdlib.h>

LbControl::LbControl(LbInstance &_instance, const LbControlConfig &config)
	:logger("control"), instance(_instance),
	 server(instance.event_loop, *this, config)
{
}

inline void
LbControl::InvalidateTranslationCache(std::span<const std::byte> payload,
				      SocketAddress address)
{
	if (payload.empty()) {
		/* flush the translation cache if the payload is empty */

#ifdef HAVE_LIBSYSTEMD
		char address_buffer[256];
		sd_journal_send("MESSAGE=control TCACHE_INVALIDATE *",
				"REMOTE_ADDR=%s",
				ToString(address_buffer, address, "?"),
				"PRIORITY=%i", LOG_DEBUG,
				nullptr);
#else
		(void)address;
#endif

		instance.FlushTranslationCaches();
		return;
	}

	const TempPoolLease tpool;
	const AllocatorPtr alloc{tpool};

	TranslationInvalidateRequest request;

	try {
		request = ParseTranslationInvalidateRequest(alloc, payload);
	} catch (...) {
		logger(2, "malformed TCACHE_INVALIDATE control packet: ",
		       GetFullMessage(std::current_exception()));
		return;
	}

#ifdef HAVE_LIBSYSTEMD
	char address_buffer[256];
	sd_journal_send("MESSAGE=control TCACHE_INVALIDATE %s", request.ToString().c_str(),
			"REMOTE_ADDR=%s",
			ToString(address_buffer, address, "?"),
			"PRIORITY=%i", LOG_DEBUG,
			nullptr);
#endif

	instance.InvalidateTranslationCaches(request);
}

inline void
LbControl::EnableNode(const char *payload, size_t length)
{
	const char *colon = (const char *)memchr(payload, ':', length);
	if (colon == nullptr || colon == payload || colon == payload + length - 1) {
		logger(3, "malformed FADE_NODE control packet: no port");
		return;
	}

	const TempPoolLease tpool;

	char *node_name = p_strndup(tpool, payload, length);
	char *port_string = node_name + (colon - payload);
	*port_string++ = 0;

	const auto *node = instance.config.FindNode(node_name);
	if (node == nullptr) {
		logger(3, "unknown node in FADE_NODE control packet");
		return;
	}

	char *endptr;
	unsigned port = strtoul(port_string, &endptr, 10);
	if (port == 0 || *endptr != 0) {
		logger(3, "malformed FADE_NODE control packet: port is not a number");
		return;
	}

	const auto with_port = node->address.WithPort(port);

	char buffer[64];
	logger(4, "enabling node ", node_name, " (",
	       ToString(buffer, with_port, "?"),
	       ")");

	instance.failure_manager.Make(with_port).UnsetAll();
}

inline void
LbControl::FadeNode(const char *payload, size_t length)
{
	const char *colon = (const char *)memchr(payload, ':', length);
	if (colon == nullptr || colon == payload || colon == payload + length - 1) {
		logger(3, "malformed FADE_NODE control packet: no port");
		return;
	}

	const TempPoolLease tpool;

	char *node_name = p_strndup(tpool, payload, length);
	char *port_string = node_name + (colon - payload);
	*port_string++ = 0;

	const auto *node = instance.config.FindNode(node_name);
	if (node == nullptr) {
		logger(3, "unknown node in FADE_NODE control packet");
		return;
	}

	char *endptr;
	unsigned port = strtoul(port_string, &endptr, 10);
	if (port == 0 || *endptr != 0) {
		logger(3, "malformed FADE_NODE control packet: port is not a number");
		return;
	}

	const auto with_port = node->address.WithPort(port);

	char buffer[64];
	logger(4, "fading node ", node_name, " (",
	       ToString(buffer, with_port, "?"),
	       ")");

	/* set status "FADE" for 3 hours */
	instance.failure_manager.Make(with_port)
		.SetFade(GetEventLoop().SteadyNow(), std::chrono::hours(3));
}

static constexpr const char *
failure_status_to_string(FailureStatus status) noexcept
{
	switch (status) {
	case FailureStatus::OK:
		return "ok";

	case FailureStatus::FADE:
		return "fade";

	case FailureStatus::PROTOCOL:
	case FailureStatus::CONNECT:
	case FailureStatus::MONITOR:
		break;
	}

	return "error";
}

static void
node_status_response(BengControl::Server *server,
		     struct pool &pool,
		     SocketAddress address,
		     std::string_view payload, std::string_view status)
{
	size_t response_length = payload.size() + 1 + status.size();
	char *response = PoolAlloc<char>(pool, response_length);

	auto *p = std::copy(payload.begin(), payload.end(), response);
	*p++ = 0;
	std::copy(status.begin(), status.end(), p);

	server->Reply(address,
		      BengControl::Command::NODE_STATUS,
		      AsBytes(std::string_view{response, response_length}));
}

inline void
LbControl::QueryNodeStatus(BengControl::Server &control_server,
			   std::string_view payload,
			   SocketAddress address)
try {
	if (address.GetSize() == 0) {
		logger(3, "got NODE_STATUS from unbound client socket");
		return;
	}

	const TempPoolLease tpool;

	const auto [_node_name, _port_string] = Split(payload, ':');
	if (_node_name.empty() || _port_string.empty()) {
		logger(3, "malformed NODE_STATUS control packet: no port");
		node_status_response(&control_server, tpool, address,
				     payload, "malformed");
		return;
	}

	char *node_name = p_strdup(tpool, _node_name);
	char *port_string = p_strdup(tpool, _port_string);

	const auto *node = instance.config.FindNode(node_name);
	if (node == nullptr) {
		logger(3, "unknown node in NODE_STATUS control packet");
		node_status_response(&control_server, tpool, address,
				     payload, "unknown");
		return;
	}

	char *endptr;
	unsigned port = strtoul(port_string, &endptr, 10);
	if (port == 0 || *endptr != 0) {
		logger(3, "malformed NODE_STATUS control packet: port is not a number");
		node_status_response(&control_server, tpool, address,
				     payload, "malformed");
		return;
	}

	const auto with_port = node->address.WithPort(port);

	auto status = instance.failure_manager.Get(GetEventLoop().SteadyNow(),
						   with_port);
	const char *s = failure_status_to_string(status);

	node_status_response(&control_server, tpool, address,
			     payload, s);
} catch (...) {
	logger(3, std::current_exception());
}

void
LbControl::OnControlPacket(BengControl::Server &control_server,
			   BengControl::Command command,
			   std::span<const std::byte> payload,
			   std::span<UniqueFileDescriptor>,
			   SocketAddress address, int uid)
{
	using namespace BengControl;

	logger(5, "command=", int(command), " uid=", uid,
	       " payload_length=", unsigned(payload.size()));

	/* only local clients are allowed to use most commands */
	const bool is_privileged = uid >= 0;

	switch (command) {
	case Command::NOP:
		break;

	case Command::TCACHE_INVALIDATE:
		InvalidateTranslationCache(payload, address);
		break;

	case Command::FADE_CHILDREN:
		break;

	case Command::DISABLE_ZEROCONF:
#ifdef HAVE_AVAHI
		if (is_privileged && instance.avahi_publisher)
			instance.avahi_publisher->HideServices();
#endif
		break;

	case Command::ENABLE_ZEROCONF:
#ifdef HAVE_AVAHI
		if (is_privileged && instance.avahi_publisher)
			instance.avahi_publisher->ShowServices();
#endif
		break;

	case Command::ENABLE_NODE:
		if (is_privileged)
			EnableNode((const char *)payload.data(), payload.size());
		break;

	case Command::FADE_NODE:
		if (is_privileged)
			FadeNode((const char *)payload.data(), payload.size());
		break;

	case Command::NODE_STATUS:
		QueryNodeStatus(control_server,
				ToStringView(payload),
				address);
		break;

	case Command::DUMP_POOLS:
		// deprecated
		break;

	case Command::VERBOSE:
		if (is_privileged && payload.size() == 1) {
			SetLogLevel(*(const uint8_t *)payload.data());
		}

		break;

	case Command::RELOAD_STATE:
		instance.ReloadState();
		break;

	case Command::FLUSH_FILTER_CACHE:
	case Command::STOPWATCH_PIPE:
	case Command::DISCARD_SESSION:
	case Command::FLUSH_HTTP_CACHE:
	case Command::TERMINATE_CHILDREN:
	case Command::ENABLE_QUEUE:
	case Command::DISABLE_QUEUE:
	case Command::DISCONNECT_DATABASE:
	case Command::DISABLE_URING:
		/* not applicable */
		break;

	case Command::FLUSH_NFS_CACHE:
	case Command::STATS:
		// deprecated
		break;
	}
}

void
LbControl::OnControlError(std::exception_ptr ep) noexcept
{
	logger(2, ep);
}

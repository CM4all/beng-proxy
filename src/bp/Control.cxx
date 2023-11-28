// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Control.hxx"
#include "Instance.hxx"
#include "session/Manager.hxx"
#include "http/cache/FilterCache.hxx"
#include "http/cache/Public.hxx"
#include "event/net/control/Server.hxx"
#include "translation/Builder.hxx"
#include "translation/Protocol.hxx"
#include "translation/InvalidateParser.hxx"
#include "pool/tpool.hxx"
#include "pool/pool.hxx"
#include "net/SocketAddress.hxx"
#include "io/Logger.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"
#include "config.h"

#ifdef HAVE_AVAHI
#include "lib/avahi/Publisher.hxx"
#endif

using namespace BengProxy;

static void
control_tcache_invalidate(BpInstance *instance, std::span<const std::byte> payload)
{
	if (payload.empty()) {
		/* flush the translation cache if the payload is empty */
		instance->FlushTranslationCaches();
		return;
	}

	if (!instance->translation_caches)
		return;

	const TempPoolLease tpool;
	const AllocatorPtr alloc{tpool};

	TranslationInvalidateRequest request;

	try {
		request = ParseTranslationInvalidateRequest(alloc, payload);
	} catch (...) {
		LogConcat(2, "control",
			  "malformed TCACHE_INVALIDATE control packet: ",
			  std::current_exception());
		return;
	}

	instance->translation_caches
		->Invalidate(request,
			     request.commands,
			     request.site);
}

static void
query_stats(BpInstance *instance, ControlServer *server,
	    SocketAddress address)
{
	const auto stats = instance->GetStats();

	try {
		server->Reply(address,
			      ControlCommand::STATS,
			      ReferenceAsBytes(stats));
	} catch (...) {
		LogConcat(3, "control", std::current_exception());
	}
}

static void
HandleStopwatchPipe(std::span<const std::byte> payload,
		    std::span<UniqueFileDescriptor> fds)
{
	if (!payload.empty() || fds.size() != 1 || !fds.front().IsPipe())
		throw std::runtime_error("Malformed STOPWATCH_PIPE packet");

	stopwatch_enable(std::move(fds.front()));
}

void
BpInstance::OnControlPacket(ControlServer &control_server,
			    BengProxy::ControlCommand command,
			    std::span<const std::byte> payload,
			    std::span<UniqueFileDescriptor> fds,
			    SocketAddress address, int uid)
{
	LogConcat(5, "control", "command=", int(command), " uid=", uid,
		  " payload_length=", unsigned(payload.size()));

	/* only local clients are allowed to use most commands */
	const bool is_privileged = uid >= 0;

	switch (command) {
	case ControlCommand::NOP:
		/* duh! */
		break;

	case ControlCommand::TCACHE_INVALIDATE:
		control_tcache_invalidate(this, payload);
		break;

	case ControlCommand::DUMP_POOLS:
		if (is_privileged)
			pool_dump_tree(root_pool);
		break;

	case ControlCommand::ENABLE_NODE:
	case ControlCommand::FADE_NODE:
	case ControlCommand::NODE_STATUS:
		/* only for beng-lb */
		break;

	case ControlCommand::STATS:
		query_stats(this, &control_server, address);
		break;

	case ControlCommand::VERBOSE:
		if (is_privileged && payload.size() == 1)
			SetLogLevel(*(const uint8_t *)payload.data());
		break;

	case ControlCommand::TERMINATE_CHILDREN:
		// TODO terminate immediately, no fade

	case ControlCommand::FADE_CHILDREN:
		if (!payload.empty())
			/* tagged fade is allowed for any unprivileged client */
			FadeTaggedChildren(ToStringView(payload));
		else if (is_privileged)
			/* unconditional fade is only allowed for privileged
			   clients */
			FadeChildren();
		break;

	case ControlCommand::DISABLE_ZEROCONF:
#ifdef HAVE_AVAHI
		if (is_privileged && avahi_publisher)
			avahi_publisher->HideServices();
#endif
		break;

	case ControlCommand::ENABLE_ZEROCONF:
#ifdef HAVE_AVAHI
		if (is_privileged && avahi_publisher)
			avahi_publisher->ShowServices();
#endif
		break;

	case ControlCommand::FLUSH_NFS_CACHE:
		// deprecated
		break;

	case ControlCommand::FLUSH_FILTER_CACHE:
		if (filter_cache != nullptr) {
			if (payload.empty())
				filter_cache_flush(*filter_cache);
			else
				filter_cache_flush_tag(*filter_cache,
						       std::string((const char *)payload.data(),
								   payload.size()));
		}

		break;

	case ControlCommand::STOPWATCH_PIPE:
		HandleStopwatchPipe(payload, fds);
		break;

	case ControlCommand::DISCARD_SESSION:
		if (!payload.empty() && session_manager)
			session_manager->DiscardAttachSession(payload);
		break;

	case ControlCommand::FLUSH_HTTP_CACHE:
		if (http_cache != nullptr)
			http_cache_flush_tag(*http_cache, ToStringView(payload));

		break;
	}
}

void
BpInstance::OnControlError(std::exception_ptr ep) noexcept
{
	LogConcat(2, "control", ep);
}

void
global_control_handler_init(BpInstance *instance)
{
	for (const auto &control_listen : instance->config.control_listen) {
		instance->control_servers.emplace_front(instance->event_loop,
							*instance,
							control_listen);
	}
}

void
global_control_handler_deinit(BpInstance *instance)
{
	instance->control_servers.clear();
}

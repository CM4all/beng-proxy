// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
#include "util/PackedBigEndian.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"
#include "config.h"

#ifdef HAVE_AVAHI
#include "lib/avahi/Publisher.hxx"
#endif

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
HandleStopwatchPipe(std::span<const std::byte> payload,
		    std::span<UniqueFileDescriptor> fds)
{
	if (!payload.empty() || fds.size() != 1 || !fds.front().IsPipe())
		throw std::runtime_error("Malformed STOPWATCH_PIPE packet");

	stopwatch_enable(std::move(fds.front()));
}

#ifdef HAVE_URING

inline void
BpInstance::OnEnableUringTimer() noexcept
{
	if (auto *u = event_loop.GetUring()) {
		uring.Enable(*u);
		fd_cache.EnableUring(*u);
	}
}

void
BpInstance::DisableUringFor(Event::Duration duration) noexcept
{
	if (duration <= Event::Duration::zero()) {
		enable_uring_timer.Cancel();
		OnEnableUringTimer();
		return;
	}

	uring.Disable();
	fd_cache.DisableUring();

	if (duration != Event::Duration::max())
		enable_uring_timer.Schedule(duration);
	else
		enable_uring_timer.Cancel();
}

inline void
BpInstance::HandleDisableUring(std::span<const std::byte> payload) noexcept
{
	if (payload.empty()) {
		DisableUringFor(Event::Duration::max());
	} else if (payload.size() == 4) {
		const uint_least32_t seconds = *reinterpret_cast<const PackedBE32 *>(payload.data());
		DisableUringFor(std::chrono::duration<uint_least32_t>{seconds});
	}
}

#endif

void
BpInstance::OnControlPacket(BengControl::Server &,
			    BengControl::Command command,
			    std::span<const std::byte> payload,
			    std::span<UniqueFileDescriptor> fds,
			    SocketAddress, int uid)
{
	using namespace BengControl;

	LogConcat(5, "control", "command=", int(command), " uid=", uid,
		  " payload_length=", unsigned(payload.size()));

	/* only local clients are allowed to use most commands */
	const bool is_privileged = uid >= 0;

	switch (command) {
	case Command::NOP:
		/* duh! */
		break;

	case Command::TCACHE_INVALIDATE:
		control_tcache_invalidate(this, payload);
		break;

	case Command::DUMP_POOLS:
		// deprecated
		break;

	case Command::ENABLE_NODE:
	case Command::FADE_NODE:
	case Command::NODE_STATUS:
		/* only for beng-lb */
		break;

	case Command::VERBOSE:
		if (is_privileged && payload.size() == 1)
			SetLogLevel(*(const uint8_t *)payload.data());
		break;

	case Command::TERMINATE_CHILDREN:
		// TODO terminate immediately, no fade

	case Command::FADE_CHILDREN:
		if (!payload.empty())
			/* tagged fade is allowed for any unprivileged client */
			FadeTaggedChildren(ToStringView(payload));
		else if (is_privileged)
			/* unconditional fade is only allowed for privileged
			   clients */
			FadeChildren();
		break;

	case Command::DISABLE_ZEROCONF:
#ifdef HAVE_AVAHI
		if (is_privileged && avahi_publisher)
			avahi_publisher->HideServices();
#endif
		break;

	case Command::ENABLE_ZEROCONF:
#ifdef HAVE_AVAHI
		if (is_privileged && avahi_publisher)
			avahi_publisher->ShowServices();
#endif
		break;

	case Command::FLUSH_NFS_CACHE:
	case Command::STATS:
		// deprecated
		break;

	case Command::FLUSH_FILTER_CACHE:
		if (filter_cache != nullptr) {
			if (payload.empty())
				filter_cache_flush(*filter_cache);
			else
				filter_cache_flush_tag(*filter_cache,
						       std::string((const char *)payload.data(),
								   payload.size()));
		}

		break;

	case Command::STOPWATCH_PIPE:
		HandleStopwatchPipe(payload, fds);
		break;

	case Command::DISCARD_SESSION:
		if (!payload.empty() && session_manager)
			session_manager->DiscardAttachSession(payload);
		break;

	case Command::FLUSH_HTTP_CACHE:
		if (http_cache != nullptr)
			http_cache_flush_tag(*http_cache, ToStringView(payload));

		break;

	case Command::RELOAD_STATE:
		ReloadState();
		break;

	case Command::DISABLE_URING:
#ifdef HAVE_URING
		HandleDisableUring(payload);
#endif
		break;

	case Command::ENABLE_QUEUE:
	case Command::DISABLE_QUEUE:
	case Command::DISCONNECT_DATABASE:
		// not applicable
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

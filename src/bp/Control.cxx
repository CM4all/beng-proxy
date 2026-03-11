// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Control.hxx"
#include "Instance.hxx"
#include "PerSite.hxx"
#include "session/Manager.hxx"
#include "http/cache/FilterCache.hxx"
#include "http/cache/Public.hxx"
#include "http/local/Address.hxx"
#include "http/local/Stock.hxx"
#include "cgi/Address.hxx"
#include "fcgi/Stock.hxx"
#include "was/Stock.hxx"
#include "was/MStock.hxx"
#include "widget/View.hxx"
#include "event/net/control/Server.hxx"
#include "translation/Builder.hxx"
#include "translation/Protocol.hxx"
#include "translation/InvalidateParser.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "pool/tpool.hxx"
#include "pool/pool.hxx"
#include "net/SocketAddress.hxx"
#include "io/Logger.hxx"
#include "util/SpanCast.hxx"
#include "util/UnalignedBigEndian.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"
#include "config.h"

#ifdef HAVE_AVAHI
#include "lib/avahi/Publisher.hxx"
#endif

inline void
BpInstance::HandleTcacheInvalidate(std::span<const std::byte> payload) noexcept
{
	if (payload.empty()) {
		/* flush the translation cache if the payload is empty */
		FlushTranslationCaches();
		return;
	}

	if (!translation_caches)
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

	translation_caches
		->Invalidate(request,
			     request.commands,
			     request.site, request.tag);
}

inline void
BpInstance::OnExpireTcacheRA(const ResourceAddress &address) noexcept
{
	switch (address.type) {
	case ResourceAddress::Type::NONE:
	case ResourceAddress::Type::LOCAL:
	case ResourceAddress::Type::HTTP:
	case ResourceAddress::Type::PIPE:
	case ResourceAddress::Type::CGI:
		// no (persistent) child process
		break;

	case ResourceAddress::Type::LHTTP:
		if (lhttp_stock) {
			const auto &lhttp = address.GetLhttp();
			const TempPoolLease tpool;
			const auto key = lhttp.GetChildId(*tpool);

			// TODO implement randomized delay
			lhttp_stock->FadeKey(key);
		}

		break;

	case ResourceAddress::Type::FASTCGI:
		if (const auto &cgi = address.GetCgi(); cgi.address_list.empty()) {
			if (fcgi_stock) {
				const TempPoolLease tpool;
				const auto key = cgi.GetChildId(*tpool);

				// TODO implement randomized delay
				fcgi_stock->FadeKey(key);
			}
		}

		break;

	case ResourceAddress::Type::WAS:
#ifdef HAVE_LIBWAS
		if (const auto &cgi = address.GetCgi(); cgi.concurrency == 0) {
			if (was_stock) {
				const TempPoolLease tpool;
				const auto key = cgi.GetChildId(*tpool);

				// TODO implement randomized delay
				was_stock->FadeKey(key);
			}
		} else if (cgi.address_list.empty()) {
			if (multi_was_stock) {
				const TempPoolLease tpool;
				const auto key = cgi.GetChildId(*tpool);

				// TODO implement randomized delay
				multi_was_stock->FadeKey(key);
			}
		}
#endif // HAVE_LIBWAS
		break;
	}
}

inline void
BpInstance::OnExpireTcache(const TranslateResponse &response) noexcept
{
	OnExpireTcacheRA(response.address);

	for (const auto &view : response.views) {
		OnExpireTcacheRA(view.address);

		for (const auto &transformation : view.transformations) {
			switch (transformation.type) {
			case Transformation::Type::PROCESS:
			case Transformation::Type::PROCESS_CSS:
			case Transformation::Type::PROCESS_TEXT:
				break;

			case Transformation::Type::FILTER:
				OnExpireTcacheRA(transformation.u.filter.address);
				break;
			}
		}
	}
}

inline void
BpInstance::HandleExpireTcacheTag(std::span<const std::byte> payload) noexcept
{
	if (payload.empty()) {
		LogConcat(2, "control",
			  "malformed EXPIRE_TCACHE_TAG control packet");
		return;
	}

	if (!translation_caches)
		return;

	// TODO implement randomized delay
	translation_caches->ExpireTag(ToStringView(payload),
				      BIND_THIS_METHOD(OnExpireTcache));
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

void
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
		const uint_least32_t seconds = ReadUnalignedBE32(payload.first<4>());
		DisableUringFor(std::chrono::duration<uint_least32_t>{seconds});
	}
}

#endif

void
BpInstance::OnControlPacket(BengControl::Command command,
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
		HandleTcacheInvalidate(payload);
		break;

	case Command::EXPIRE_TCACHE_TAG:
		HandleExpireTcacheTag(payload);
		break;

	case Command::ENABLE_NODE:
	case Command::FADE_NODE:
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

	case Command::DUMP_POOLS:
	case Command::NODE_STATUS:
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
		if (http_cache != nullptr) {
			if (payload.empty())
				http_cache_flush(*http_cache);
			else
				http_cache_flush_tag(*http_cache, ToStringView(payload));
		}

		break;

	case Command::RELOAD_STATE:
		ReloadState();
		break;

	case Command::DISABLE_URING:
#ifdef HAVE_URING
		if (is_privileged)
			HandleDisableUring(payload);
#endif
		break;

	case Command::RESET_LIMITER:
		if (!payload.empty() && per_site)
			if (auto p = per_site->Get(StringWithHash{ToStringView(payload)}))
			    p->ResetLimiter();

		break;

	case Command::REJECT_CLIENT:
	case Command::TARPIT_CLIENT:
		// TODO implement
		break;

	case Command::ENABLE_QUEUE:
	case Command::DISABLE_QUEUE:
	case Command::DISCONNECT_DATABASE:
	case Command::CANCEL_JOB:
		// not applicable
		break;
	}
}

void
BpInstance::OnControlError(std::exception_ptr &&error) noexcept
{
	LogConcat(2, "control", std::move(error));
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

/*
 * Copyright 2007-2019 Content Management AG
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

#include "Control.hxx"
#include "Instance.hxx"
#include "fcache.hxx"
#include "nfs/Cache.hxx"
#include "control/Distribute.hxx"
#include "control/Server.hxx"
#include "control/Local.hxx"
#include "translation/Request.hxx"
#include "translation/Cache.hxx"
#include "translation/Protocol.hxx"
#include "translation/InvalidateParser.hxx"
#include "pool/tpool.hxx"
#include "pool/pool.hxx"
#include "net/UdpDistribute.hxx"
#include "net/SocketAddress.hxx"
#include "net/IPv4Address.hxx"
#include "io/Logger.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Exception.hxx"
#include "util/Macros.hxx"
#include "util/WritableBuffer.hxx"
#include "stopwatch.hxx"

#include <assert.h>
#include <string.h>

using namespace BengProxy;

static void
control_tcache_invalidate(BpInstance *instance, ConstBuffer<void> payload)
{
	if (payload.empty()) {
		/* flush the translation cache if the payload is empty */
		instance->FlushTranslationCaches();
		return;
	}

	if (instance->translation_cache == nullptr)
		return;

	const TempPoolLease tpool;

	TranslationInvalidateRequest request;

	try {
		request = ParseTranslationInvalidateRequest(*tpool,
							    payload.data, payload.size);
	} catch (...) {
		LogConcat(2, "control",
			  "malformed TCACHE_INVALIDATE control packet: ",
			  std::current_exception());
		return;
	}

	instance->translation_cache->Invalidate(request,
						ConstBuffer<TranslationCommand>(request.commands.raw(),
										request.commands.size()),
						request.site);
}

static void
query_stats(BpInstance *instance, ControlServer *server,
	    SocketAddress address)
{
	if (address.GetSize() == 0)
		/* TODO: this packet was forwarded by the master process, and
		   has no source address; however, the master process must get
		   statistics from all worker processes (even those that have
		   exited already) */
		return;

	const auto stats = instance->GetStats();

	try {
		server->Reply(address,
			      ControlCommand::STATS, &stats, sizeof(stats));
	} catch (...) {
		LogConcat(3, "control", std::current_exception());
	}
}

static void
HandleStopwatchPipe(ConstBuffer<void> payload,
		    WritableBuffer<UniqueFileDescriptor> fds)
{
	if (!payload.empty() || fds.size != 1 || !fds.front().IsPipe())
		throw std::runtime_error("Malformed STOPWATCH_PIPE packet");

	stopwatch_enable(std::move(fds.front()));
}

void
BpInstance::OnControlPacket(ControlServer &control_server,
			    BengProxy::ControlCommand command,
			    ConstBuffer<void> payload,
			    WritableBuffer<UniqueFileDescriptor> fds,
			    SocketAddress address, int uid)
{
	LogConcat(5, "control", "command=", int(command), " uid=", uid,
		  " payload_length=", unsigned(payload.size));

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
		if (is_privileged && payload.size == 1)
			SetLogLevel(*(const uint8_t *)payload.data);
		break;

	case ControlCommand::FADE_CHILDREN:
		if (!payload.empty())
			/* tagged fade is allowed for any unprivileged client */
			FadeTaggedChildren(std::string((const char *)payload.data,
						       payload.size).c_str());
		else if (is_privileged)
			/* unconditional fade is only allowed for privileged
			   clients */
			FadeChildren();
		break;

	case ControlCommand::DISABLE_ZEROCONF:
#ifdef HAVE_AVAHI
		if (is_privileged)
			avahi_client.HideServices();
#endif
		break;

	case ControlCommand::ENABLE_ZEROCONF:
#ifdef HAVE_AVAHI
		if (is_privileged)
			avahi_client.ShowServices();
#endif
		break;

	case ControlCommand::FLUSH_NFS_CACHE:
#ifdef HAVE_LIBNFS
		if (nfs_cache != nullptr)
			nfs_cache_flush(*nfs_cache);
#endif
		break;

	case ControlCommand::FLUSH_FILTER_CACHE:
		if (filter_cache != nullptr) {
			if (payload.empty())
				filter_cache_flush(*filter_cache);
			else
				filter_cache_flush_tag(*filter_cache,
						       std::string((const char *)payload.data,
								   payload.size).c_str());
		}

		break;

	case ControlCommand::STOPWATCH_PIPE:
		HandleStopwatchPipe(payload, fds);
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
	if (instance->config.control_listen.empty())
		return;

	instance->control_distribute = new ControlDistribute(instance->event_loop,
							     *instance);

	for (const auto &control_listen : instance->config.control_listen) {
		instance->control_servers.emplace_front(instance->event_loop,
							*instance->control_distribute,
							control_listen);
	}
}

void
global_control_handler_deinit(BpInstance *instance)
{
	instance->control_servers.clear();
	delete instance->control_distribute;
}

void
global_control_handler_enable(BpInstance &instance)
{
	for (auto &c : instance.control_servers)
		c.Enable();
}

void
global_control_handler_disable(BpInstance &instance)
{
	for (auto &c : instance.control_servers)
		c.Disable();
}

UniqueSocketDescriptor
global_control_handler_add_fd(BpInstance *instance)
{
	assert(!instance->control_servers.empty());
	assert(instance->control_distribute != nullptr);

	return instance->control_distribute->Add();
}

void
global_control_handler_set_fd(BpInstance *instance,
			      UniqueSocketDescriptor &&fd)
{
	assert(!instance->control_servers.empty());
	assert(instance->control_distribute != nullptr);

	instance->control_distribute->Clear();

	/* erase all */
	instance->control_servers.clear();

	/* create new one with the given socket */
	instance->control_servers.emplace_front(instance->event_loop,
						std::move(fd),
						*instance->control_distribute);
}

/*
 * local (implicit) control channel
 */

void
local_control_handler_init(BpInstance *instance)
{
	instance->local_control_server =
		std::make_unique<LocalControl>("beng_control:pid=", *instance);
}

void
local_control_handler_deinit(BpInstance *instance)
{
	instance->local_control_server.reset();
}

void
local_control_handler_open(BpInstance *instance)
{
	instance->local_control_server->Open(instance->event_loop);
}

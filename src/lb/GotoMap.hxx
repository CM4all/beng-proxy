/*
 * Copyright 2007-2019 CM4all GmbH
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

#pragma once

#include "LuaInitHook.hxx"
#include "util/Compiler.h"

#include <map>

struct LbConfig;
struct LbGoto;
struct LbGotoConfig;
struct LbClusterConfig;
struct LbBranchConfig;
struct LbTranslationHandlerConfig;
struct LbLuaHandlerConfig;
struct TranslationInvalidateRequest;
class FailureManager;
class MyAvahiClient;
class EventLoop;
class LbCluster;
class LbBranch;
class LbTranslationHandler;
class LbLuaHandler;
class LbMonitorManager;

class LbGotoMap final {
	const LbConfig &root_config;
	FailureManager &failure_manager;
	LbMonitorManager &monitors;
#ifdef HAVE_AVAHI
	MyAvahiClient &avahi_client;
#endif
	EventLoop &event_loop;

	LbLuaInitHook lua_init_hook;

	std::map<const LbClusterConfig *, LbCluster> clusters;
	std::map<const LbBranchConfig *, LbBranch> branches;
	std::map<const LbTranslationHandlerConfig *,
		 LbTranslationHandler> translation_handlers;
	std::map<const LbLuaHandlerConfig *,
		 LbLuaHandler> lua_handlers;

public:
	LbGotoMap(const LbConfig &_config,
		  FailureManager &_failure_manager,
		  LbMonitorManager &_monitors,
#ifdef HAVE_AVAHI
		  MyAvahiClient &_avahi_client,
#endif
		  EventLoop &_event_loop) noexcept;

	~LbGotoMap() noexcept;

	LbGotoMap(const LbGotoMap &) = delete;
	LbGotoMap &operator=(const LbGotoMap &) = delete;

	void Clear();

	void FlushCaches();
	void InvalidateTranslationCaches(const TranslationInvalidateRequest &request);

	gcc_pure
	size_t GetAllocatedTranslationCacheMemory() const noexcept;

	LbGoto GetInstance(const char *name);
	LbGoto GetInstance(const LbGotoConfig &config);

	LbCluster &GetInstance(const LbClusterConfig &config);

private:
	LbBranch &GetInstance(const LbBranchConfig &config);
	LbLuaHandler &GetInstance(const LbLuaHandlerConfig &config);
	LbTranslationHandler &GetInstance(const LbTranslationHandlerConfig &config);
};

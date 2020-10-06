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

#include "GotoMap.hxx"
#include "Goto.hxx"
#include "Cluster.hxx"
#include "Branch.hxx"
#include "TranslationHandler.hxx"
#include "LuaHandler.hxx"
#include "Config.hxx"
#include "MonitorManager.hxx"

LbGotoMap::LbGotoMap(const LbConfig &_config,
		     LbContext _context,
		     EventLoop &_event_loop) noexcept
	:LbContext(_context),
	 root_config(_config),
	 event_loop(_event_loop),
	 lua_init_hook(this) {}

LbGotoMap::~LbGotoMap() noexcept
{
}

void
LbGotoMap::Clear()
{
	translation_handlers.clear();
	clusters.clear();
}

void
LbGotoMap::FlushCaches()
{
	for (auto &i : translation_handlers)
		i.second.FlushCache();
}

void
LbGotoMap::InvalidateTranslationCaches(const TranslationInvalidateRequest &request)
{
	for (auto &i : translation_handlers)
		i.second.InvalidateCache(request);
}

size_t
LbGotoMap::GetAllocatedTranslationCacheMemory() const noexcept
{
	size_t result = 0;
	for (const auto &i : translation_handlers)
		result += i.second.GetAllocatedCacheMemory();
	return result;
}

LbGoto
LbGotoMap::GetInstance(const char *name)
{
	return GetInstance(root_config.FindGoto(name));
}

LbGoto
LbGotoMap::GetInstance(const LbGotoConfig &config)
{
	struct GetInstanceHelper {
		LbGotoMap &map;

		LbGoto operator()(std::nullptr_t) const noexcept {
			return {};
		}

		LbGoto operator()(const LbClusterConfig *cluster) const {
			return map.GetInstance(*cluster);
		}

		LbGoto operator()(const LbBranchConfig *branch) const {
			return map.GetInstance(*branch);
		}

		LbGoto operator()(const LbLuaHandlerConfig *lua) const {
			return map.GetInstance(*lua);
		}

		LbGoto operator()(const LbTranslationHandlerConfig *translation) const {
			return map.GetInstance(*translation);
		}

		LbGoto operator()(const LbSimpleHttpResponse &response) const noexcept {
			return response;
		}
	};

	return std::visit(GetInstanceHelper{*this}, config.destination);
}

LbCluster &
LbGotoMap::GetInstance(const LbClusterConfig &config)
{
	const LbContext &context = *this;

	auto *monitor_stock = config.monitor != nullptr
		? &monitors[*config.monitor]
		: nullptr;

	return clusters.emplace(std::piecewise_construct,
				std::forward_as_tuple(&config),
				std::forward_as_tuple(config, context,
						      monitor_stock))
		.first->second;
}

LbBranch &
LbGotoMap::GetInstance(const LbBranchConfig &config)
{
	return branches.emplace(std::piecewise_construct,
				std::forward_as_tuple(&config),
				std::forward_as_tuple(*this, config))
		.first->second;
}

LbLuaHandler &
LbGotoMap::GetInstance(const LbLuaHandlerConfig &config)
{
	return lua_handlers.emplace(std::piecewise_construct,
				    std::forward_as_tuple(&config),
				    std::forward_as_tuple(lua_init_hook, config))
		.first->second;
}

LbTranslationHandler &
LbGotoMap::GetInstance(const LbTranslationHandlerConfig &config)
{
	return translation_handlers.emplace(std::piecewise_construct,
					    std::forward_as_tuple(&config),
					    std::forward_as_tuple(event_loop,
								  *this, config))
		.first->second;
}

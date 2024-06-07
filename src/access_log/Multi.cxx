// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Multi.hxx"
#include "Glue.hxx"
#include "Config.hxx"

MultiAccessLogGlue::MultiAccessLogGlue() noexcept = default;
MultiAccessLogGlue::~MultiAccessLogGlue() noexcept = default;

AccessLogGlue *
MultiAccessLogGlue::Make(const MultiAccessLogConfig &multi_config, const UidGid *user,
			 std::string_view name)
{
	if (auto i = map.find(name); i != map.end())
		return i->second.get();

	const auto *config = multi_config.Find(name);
	if (config == nullptr)
		return nullptr;

	auto *glue = AccessLogGlue::Create(*config, user);
	map.emplace(name, glue);
	return glue;
}

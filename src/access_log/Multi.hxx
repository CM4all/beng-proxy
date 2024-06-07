// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <map>
#include <memory>

struct MultiAccessLogConfig;
struct UidGid;
class AccessLogGlue;

/**
 * Container for multiple named #AccessLogGlue instances.
 */
class MultiAccessLogGlue {
	std::map<std::string, std::unique_ptr<AccessLogGlue>, std::less<>> map;

public:
	MultiAccessLogGlue() noexcept;
	~MultiAccessLogGlue() noexcept;

	/**
	 * Return the (shared) #AccessLogGlue instance with the
	 * specified name.  If no such instance exists, one is
	 * created.  Returns nullptr if no configuration with this
	 * name exists.
	 */
	AccessLogGlue *Make(const MultiAccessLogConfig &config, const UidGid *user,
			    std::string_view name);
};

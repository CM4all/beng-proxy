// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SpawnStats.hxx"
#include "spawn/Stats.hxx"
#include "memory/GrowingBuffer.hxx"

using std::string_view_literals::operator""sv;

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view process,
      const SpawnStats &stats) noexcept
{
	buffer.Fmt(R"(
# HELP beng_proxy_children_spawned Total number of child processes spawned
# TYPE beng_proxy_children_spawned counter

# HELP beng_proxy_spawn_errors Total number of child processes that failed to spawn
# TYPE beng_proxy_spawn_errors counter

# HELP beng_proxy_children_killed Total number of child processes that were killed with a signal
# TYPE beng_proxy_children_killed counter

# HELP beng_proxy_children_exited Total number of child processes that have exited
# TYPE beng_proxy_children_exited counter

# HELP beng_proxy_children Number of child processes
# TYPE beng_proxy_children gauge

beng_proxy_children_spawned{{process={:?}}} {}
beng_proxy_spawn_errors{{process={:?}}} {}
beng_proxy_children_killed{{process={:?}}} {}
beng_proxy_children_exited{{process={:?}}} {}
beng_proxy_children{{process={:?}}} {}
)"sv,
		   process, stats.spawned,
		   process, stats.errors,
		   process, stats.killed,
		   process, stats.exited,
		   process, stats.alive);
}

} // namespace Prometheus

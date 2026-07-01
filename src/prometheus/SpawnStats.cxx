// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SpawnStats.hxx"
#include "spawn/Stats.hxx"
#include "memory/GrowingBuffer.hxx"
#include "time/Cast.hxx"

using std::string_view_literals::operator""sv;

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view process,
      const ChildProcessTerminatorStats &stats) noexcept
{
	buffer.Fmt(R"(
# HELP beng_proxy_terminator_signals Number of signals sent to child processes
# TYPE beng_proxy_terminator_signals counter

# HELP beng_proxy_terminator_failed_signals Number of failures to send signals to child processes
# TYPE beng_proxy_terminator_failed_signals counter

# HELP beng_proxy_terminator_exits Number of child terminated child processes that have exited
# TYPE beng_proxy_terminator_exits counter

# HELP beng_proxy_terminator_timeouts Number of child terminated child processes that have not within the timeout
# TYPE beng_proxy_terminator_timeouts counter

# HELP beng_proxy_terminator_duration Total duration waiting for terminated child processes to exit
# TYPE beng_proxy_terminator_duration counter

beng_proxy_terminator_signals{{process={:?}}} {}
beng_proxy_terminator_failed_signals{{process={:?}}} {}
beng_proxy_terminator_exits{{process={:?}}} {}
beng_proxy_terminator_timeouts{{process={:?}}} {}
beng_proxy_terminator_duration{{process={:?}}} {:e}
)"sv,
		   process, stats.n_signals,
		   process, stats.n_failed_signals,
		   process, stats.n_exits,
		   process, stats.n_timeouts,
		   process, ToFloatSeconds(stats.total_shutdown_duration));
}

void
Write(GrowingBuffer &buffer, std::string_view process,
      const SpawnStats &stats) noexcept
{
	buffer.Fmt(R"(
# HELP beng_proxy_spawn_pending Number of child processes being spawned currently
# TYPE beng_proxy_spawn_pending gauge

# HELP beng_proxy_children_spawned Total number of child processes spawned
# TYPE beng_proxy_children_spawned counter

# HELP beng_proxy_total_spawn_duration Total duration for spawning child processes
# TYPE beng_proxy_total_spawn_duration counter

# HELP beng_proxy_spawn_errors Total number of child processes that failed to spawn
# TYPE beng_proxy_spawn_errors counter

# HELP beng_proxy_children_killed Total number of child processes that were killed with a signal
# TYPE beng_proxy_children_killed counter

# HELP beng_proxy_children_exited Total number of child processes that have exited
# TYPE beng_proxy_children_exited counter

# HELP beng_proxy_children Number of child processes
# TYPE beng_proxy_children gauge

beng_proxy_spawn_pending{{process={:?}}} {}
beng_proxy_children_spawned{{process={:?}}} {}
beng_proxy_total_spawn_duration{{process={:?}}} {:e}
beng_proxy_spawn_errors{{process={:?}}} {}
beng_proxy_children_killed{{process={:?}}} {}
beng_proxy_children_exited{{process={:?}}} {}
beng_proxy_children{{process={:?}}} {}
)"sv,
		   process, stats.pending,
		   process, stats.spawned,
		   process, ToFloatSeconds(stats.total_spawn_duration),
		   process, stats.errors,
		   process, stats.killed,
		   process, stats.exited,
		   process, stats.alive);
}

} // namespace Prometheus

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CgroupPressureStats.hxx"
#include "stats/CgroupPressureStats.hxx"
#include "memory/GrowingBuffer.hxx"
#include "time/Cast.hxx"

using std::string_view_literals::operator""sv;

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view resource,
      const CgroupPressureStats &stats) noexcept
{
	buffer.Fmt(R"(
# HELP beng_proxy_cgroup_pressure_throttled Total number of throttled spawn requests
# TYPE beng_proxy_cgroup_pressure_throttled counter

# HELP beng_proxy_cgroup_pressure_canceled Total number of throttled spawn requests that were canceled
# TYPE beng_proxy_cgroup_pressure_canceled counter

# HELP beng_proxy_cgroup_pressure_throttle_duration Total time spawn requests were throttled
# TYPE beng_proxy_cgroup_pressure_throttle_duration counter

beng_proxy_cgroup_pressure_throttled{{resource={:?}}} {}
beng_proxy_cgroup_pressure_canceled{{resource={:?}}} {}
beng_proxy_cgroup_pressure_throttle_duration{{resource={:?}}} {:e}
)"sv,
		   resource, stats.n_throttled,
		   resource, stats.n_canceled,
		   resource, ToFloatSeconds(stats.total_throttle_duration));
}

} // namespace Prometheus

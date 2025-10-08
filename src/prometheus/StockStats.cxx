// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "StockStats.hxx"
#include "stock/Stats.hxx"
#include "memory/GrowingBuffer.hxx"

using std::string_view_literals::operator""sv;

namespace Prometheus {

void
Write(GrowingBuffer &buffer, std::string_view process,
      std::string_view stock,
      const StockStats &stats) noexcept
{
	buffer.Fmt(R"(
# HELP beng_proxy_stock_busy Number of busy stock items
# TYPE beng_proxy_stock_busy gauge

# HELP beng_proxy_stock_idle Number of idle stock items
# TYPE beng_proxy_stock_idle gauge

beng_proxy_stock_busy{{process={:?},stock={:?}}} {}
beng_proxy_stock_idle{{process={:?},stock={:?}}} {}
)"sv,
		   process, stock, stats.busy,
		   process, stock, stats.idle);
}

} // namespace Prometheus

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "JvmRoute.hxx"
#include "ClusterConfig.hxx"
#include "http/CommonHeaders.hxx"
#include "http/CookieExtract.hxx"
#include "util/StringSplit.hxx"
#include "strmap.hxx"

using std::string_view_literals::operator""sv;

sticky_hash_t
lb_jvm_route_get(const StringMap &request_headers,
		 const LbClusterConfig &cluster) noexcept
{
	const char *cookie = request_headers.Get(cookie_header);
	if (cookie == NULL)
		return 0;

	const auto jsessionid = ExtractCookieRaw(cookie, "JSESSIONID"sv);
	const auto jvm_route = Split(jsessionid, '.').second;
	if (jvm_route.empty())
		return 0;

	int i = cluster.FindJVMRoute(jvm_route);
	if (i < 0)
		return 0;

	/* add num_members to make sure that the modulo still maps to the
	   node index, but the first node is not referred to as zero
	   (special value for "no session") */
	return i + cluster.members.size();
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Config.hxx"
#include "CommandLine.hxx"
#include "pg/Interval.hxx"
#include "net/Parser.hxx"
#include "util/StringAPI.hxx"
#include "util/StringParser.hxx"

#include <stdexcept>

using std::string_view_literals::operator""sv;

void
BpConfig::HandleSet(std::string_view name, const char *value)
{
	if (name == "max_connections"sv) {
		max_connections = ParsePositiveLong(value, 1024 * 1024);
	} else if (name == "tcp_stock_limit"sv) {
		tcp_stock_limit = ParseUnsignedLong(value);
	} else if (name == "lhttp_stock_limit"sv) {
		lhttp_stock_limit = ParseUnsignedLong(value);
	} else if (name == "lhttp_stock_max_idle"sv) {
		lhttp_stock_max_idle = ParseUnsignedLong(value);
	} else if (name == "fastcgi_stock_limit"sv) {
		fcgi_stock_limit = ParseUnsignedLong(value);
	} else if (name == "fcgi_stock_max_idle"sv) {
		fcgi_stock_max_idle = ParseUnsignedLong(value);
	} else if (name == "was_stock_limit"sv) {
		was_stock_limit = ParseUnsignedLong(value);
	} else if (name == "was_stock_max_idle"sv) {
		was_stock_max_idle = ParseUnsignedLong(value);
	} else if (name == "multi_was_stock_limit"sv) {
		multi_was_stock_limit = ParseUnsignedLong(value);
	} else if (name == "multi_was_stock_max_idle"sv) {
		multi_was_stock_max_idle = ParseUnsignedLong(value);
	} else if (name == "remote_was_stock_limit"sv) {
		remote_was_stock_limit = ParseUnsignedLong(value);
	} else if (name == "remote_was_stock_max_idle"sv) {
		remote_was_stock_max_idle = ParseUnsignedLong(value);
	} else if (name == "http_cache_size"sv) {
		http_cache_size = ParseSize(value);
	} else if (name == "http_cache_obey_no_cache"sv) {
		http_cache_obey_no_cache = ParseBool(value);
	} else if (name == "filter_cache_size"sv) {
		filter_cache_size = ParseSize(value);
	} else if (name == "encoding_cache_size"sv) {
		encoding_cache_size = ParseSize(value);
	} else if (name == "nfs_cache_size"sv) {
		/* deprecated */
	} else if (name == "translate_cache_size"sv) {
		translate_cache_size = ParseUnsignedLong(value);
	} else if (name == "translate_stock_limit"sv) {
		translate_stock_limit = ParseUnsignedLong(value);
	} else if (name == "stopwatch"sv) {
		/* deprecated */
	} else if (name == "dump_widget_tree"sv) {
		/* deprecated */
	} else if (name == "use_xattr"sv) {
		use_xattr = ParseBool(value);
	} else if (name == "use_io_uring"sv) {
		use_io_uring = ParseBool(value);
	} else if (name == "io_uring_sqpoll"sv) {
		io_uring_sqpoll = ParseBool(value);
	} else if (name == "io_uring_sq_thread_cpu"sv) {
		io_uring_sq_thread_cpu = ParseUnsignedLong(value);
	} else if (name == "verbose_response"sv) {
		verbose_response = ParseBool(value);
	} else if (name == "session_cookie"sv) {
		if (*value == 0)
			throw std::runtime_error("Invalid value");

		session_cookie = value;
	} else if (name == "session_cookie_same_site"sv) {
		session_cookie_same_site = ParseCookieSameSite(value);
	} else if (name == "dynamic_session_cookie"sv) {
		dynamic_session_cookie = ParseBool(value);
	} else if (name == "session_idle_timeout"sv) {
		session_idle_timeout = Pg::ParseIntervalS(value);
	} else if (name == "session_save_path"sv) {
		session_save_path = value;
	} else
		throw std::runtime_error("Unknown variable");
}

void
BpConfig::Finish(unsigned default_port)
{
	for (auto &i : listen) {
		/* reverse the list because our ConfigParser always
		   inserts at the front */
		i.translation_sockets.reverse();
	}

	if (listen.empty())
		listen.emplace_front(ParseSocketAddress("*", default_port, true));

	if (translation_sockets.empty()) {
		translation_sockets.emplace_front("@translation"sv);
	} else
		/* reverse the list because our ConfigParser always
		   inserts at the front */
		translation_sockets.reverse();

	/* run the spawner as a separate user (privilege
	   separation) */
	if (!debug_mode && spawn.spawner_uid_gid.IsEmpty())
		spawn.spawner_uid_gid.Lookup("cm4all-beng-spawn");

	if (spawn.default_uid_gid.IsEmpty())
		spawn.default_uid_gid.LoadEffective();
}

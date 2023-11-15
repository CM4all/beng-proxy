// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "NameCache.hxx"
#include "certdb/Config.hxx"

#include <cassert>

using std::string_view_literals::operator""sv;

CertNameCache::CertNameCache(EventLoop &event_loop,
			     const CertDatabaseConfig &config,
			     CertNameCacheHandler &_handler) noexcept
	:logger("CertNameCache"), handler(_handler),
	 conn(event_loop, config.connect.c_str(), config.schema.c_str(), *this),
	 update_timer(event_loop, BIND_THIS_METHOD(OnUpdateTimer))
{
}

bool
CertNameCache::Lookup(const char *_host) const noexcept
{
	if (!complete)
		/* we can't give reliable results until the cache is
		   complete */
		return true;

	const std::string host(_host);

	const std::scoped_lock lock{mutex};
	return names.find(host) != names.end() ||
		alt_names.find(host) != alt_names.end();
}

void
CertNameCache::OnUpdateTimer() noexcept
try {
	assert(conn.IsReady());

	if (!conn.IsIdle()) {
		/* still processing a query; try again later */
		ScheduleUpdate();
		return;
	}

	logger(4, "updating certificate database name cache");

	n_added = n_updated = n_deleted = 0;

	if (complete)
		conn.SendQuery(*this,
			       "SELECT common_name, "
			       "server_certificate_alt_name.name, "
			       "modified, deleted "
			       " FROM server_certificate LEFT JOIN server_certificate_alt_name"
			       " ON server_certificate.id=server_certificate_alt_name.server_certificate_id"
			       " WHERE modified>$1"
			       " ORDER BY modified",
			       latest.c_str());
	else
		/* omit deleted certificates during the initial download
		   (until our mirror is complete) */
		conn.SendQuery(*this,
			       "SELECT common_name, "
			       "server_certificate_alt_name.name, "
			       "modified "
			       " FROM server_certificate LEFT JOIN server_certificate_alt_name"
			       " ON server_certificate.id=server_certificate_alt_name.server_certificate_id"
			       " WHERE NOT deleted"
			       " ORDER BY modified");

	conn.SetSingleRowMode();
} catch (...) {
	conn.CheckError(std::current_exception());
}

void
CertNameCache::ScheduleUpdate() noexcept
{
	if (!update_timer.IsPending())
		update_timer.Schedule(std::chrono::milliseconds(200));
}

inline void
CertNameCache::AddAltName(const std::string &common_name,
			  std::string &&alt_name) noexcept
{
	/* create the alt_name if it doesn't exist yet */
	auto i = alt_names.emplace(std::move(alt_name), std::set<std::string>());
	/* add the common_name to the set */
	i.first->second.emplace(common_name);
}

inline void
CertNameCache::RemoveAltName(const std::string &common_name,
			     const std::string &alt_name) noexcept
{
	auto i = alt_names.find(alt_name);
	if (i != alt_names.end()) {
		/* this alt_name exists */
		auto j = i->second.find(common_name);
		if (j != i->second.end()) {
			/* and inside, the given common_name exist; remove
			   it */
			i->second.erase(j);
			if (i->second.empty())
				/* list is empty, no more certificates cover this
				   alt_name: remove it completely */
				alt_names.erase(i);
		}
	}
}

static void
Listen(Pg::AsyncConnection &c, const char *name)
{
	std::string sql("LISTEN \"");

	const auto &schema = c.GetSchemaName();
	if (!schema.empty() && schema != "public") {
		/* prefix the notify name unless we're in the default
		   schema */
		sql += schema;
		sql += ':';
	}

	sql += name;
	sql += "\"";

	c.Execute(sql.c_str());
}

void
CertNameCache::OnConnect()
{
	logger(5, "connected to certificate database");

	// TODO: make asynchronous
	Listen(conn, "modified");
	Listen(conn, "deleted");

	ScheduleUpdate();
}

void
CertNameCache::OnDisconnect() noexcept
{
	logger(4, "disconnected from certificate database");

	UnscheduleUpdate();
}

void
CertNameCache::OnNotify(const char *name)
{
	logger(5, "received notify '", name, "'");

	ScheduleUpdate();
}

void
CertNameCache::OnError(std::exception_ptr e) noexcept
{
	logger(1, e);
}

void
CertNameCache::OnResult(Pg::Result &&result)
{
	if (result.IsError()) {
		logger(1, "query error from certificate database: ",
		       result.GetErrorMessage());
		ScheduleUpdate();
		return;
	}

	const char *modified = nullptr;

	for (const auto &row : result) {
		std::string name{row.GetValueView(0)};
		std::string alt_name{row.GetValueView(1)};
		modified = row.GetValue(2);
		const bool deleted = complete && *row.GetValue(3) == 't';

		handler.OnCertModified(name, deleted);
		if (!alt_name.empty())
			handler.OnCertModified(alt_name, deleted);

		const std::scoped_lock lock{mutex};

		if (deleted) {
			if (!alt_name.empty())
				RemoveAltName(name, std::move(alt_name));

			auto i = names.find(std::move(name));
			if (i != names.end()) {
				names.erase(i);
				++n_deleted;
			}
		} else {
			if (!alt_name.empty())
				AddAltName(name, std::move(alt_name));

			auto i = names.emplace(std::move(name));
			if (i.second)
				++n_added;
			else
				++n_updated;
		}
	}

	if (modified != nullptr)
		latest = modified;
}

void
CertNameCache::OnResultEnd()
{
	logger.Fmt(4, "certificate database name cache: {} added, {} updated, {} deleted"sv,
		   n_added, n_updated, n_deleted);

	if (!complete) {
		logger(4, "certificate database name cache is complete");
		complete = true;
	}
}

void
CertNameCache::OnResultError() noexcept
{
	ScheduleUpdate();
}

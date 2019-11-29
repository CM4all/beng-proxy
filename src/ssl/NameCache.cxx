/*
 * Copyright 2007-2017 Content Management AG
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

#include "NameCache.hxx"
#include "certdb/Config.hxx"

#include <assert.h>
#include <string.h>

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

	const std::unique_lock<std::mutex> lock(mutex);
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
			       "ARRAY(SELECT name FROM server_certificate_alt_name WHERE server_certificate_id=server_certificate.id), "
			       "modified, deleted "
			       " FROM server_certificate"
			       " WHERE modified>$1"
			       " ORDER BY modified",
			       latest.c_str());
	else
		/* omit deleted certificates during the initial download
		   (until our mirror is complete) */
		conn.SendQuery(*this,
			       "SELECT common_name, "
			       "ARRAY(SELECT name FROM server_certificate_alt_name WHERE server_certificate_id=server_certificate.id), "
			       "modified "
			       " FROM server_certificate"
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
CertNameCache::AddAltNames(const std::string &common_name,
			   std::forward_list<std::string> &&list) noexcept
{
	for (auto &&a : list) {
		/* create the alt_name if it doesn't exist yet */
		auto i = alt_names.emplace(std::move(a), std::set<std::string>());
		/* add the common_name to the set */
		i.first->second.emplace(common_name);
	}
}

inline void
CertNameCache::RemoveAltNames(const std::string &common_name,
			      std::forward_list<std::string> &&list) noexcept
{
	for (auto &&a : list) {
		auto i = alt_names.find(std::move(a));
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
		std::string name(row.GetValue(0));
		auto _alt_names = row.IsValueNull(1)
			? std::forward_list<std::string>()
			: Pg::DecodeArray(row.GetValue(1));
		modified = row.GetValue(2);
		const bool deleted = complete && *row.GetValue(3) == 't';

		handler.OnCertModified(name, deleted);
		for (const auto &a : _alt_names)
			handler.OnCertModified(a, deleted);

		const std::unique_lock<std::mutex> lock(mutex);

		if (deleted) {
			RemoveAltNames(name, std::move(_alt_names));

			auto i = names.find(std::move(name));
			if (i != names.end()) {
				names.erase(i);
				++n_deleted;
			}
		} else {
			AddAltNames(name, std::move(_alt_names));

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
	logger.Format(4, "certificate database name cache: %u added, %u updated, %u deleted",
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

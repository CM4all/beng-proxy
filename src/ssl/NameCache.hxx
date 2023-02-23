// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "pg/AsyncConnection.hxx"
#include "event/FineTimerEvent.hxx"
#include "io/Logger.hxx"

#include <unordered_set>
#include <unordered_map>
#include <set>
#include <string>
#include <mutex>

struct CertDatabaseConfig;

class CertNameCacheHandler {
public:
	virtual void OnCertModified(const std::string &name,
				    bool deleted) noexcept = 0;
};

/**
 * A frontend for #CertDatabase which establishes a cache of all host
 * names and keeps it up to date.
 *
 * All modifications run asynchronously in the main thread, and
 * std::unordered_set queries may be executed from any thread
 * (protected by the mutex).
 */
class CertNameCache final : Pg::AsyncConnectionHandler, Pg::AsyncResultHandler {
	const LLogger logger;

	CertNameCacheHandler &handler;

	Pg::AsyncConnection conn;

	FineTimerEvent update_timer;

	mutable std::mutex mutex;

	/**
	 * A list of host names found in the database.
	 */
	std::unordered_set<std::string> names;

	/**
	 * A list of alt_names found in the database.  Each alt_name maps
	 * to a list of common_name values it appears in.
	 */
	std::unordered_map<std::string, std::set<std::string>> alt_names;

	/**
	 * The latest timestamp seen in a record.  This is used for
	 * incremental updates.
	 */
	std::string latest = "1971-01-01";

	unsigned n_added, n_updated, n_deleted;

	/**
	 * This flag is set to true as soon as the cached name list has
	 * become complete for the first time.  With an incomplete cache,
	 * Lookup() will always return true, because we don't know yet if
	 * the desired name is just not yet loaded.
	 */
	bool complete = false;

public:
	CertNameCache(EventLoop &event_loop,
		      const CertDatabaseConfig &config,
		      CertNameCacheHandler &_handler) noexcept;

	auto &GetEventLoop() const noexcept {
		return update_timer.GetEventLoop();
	}

	void Connect() noexcept {
		conn.Connect();
	}

	void Disconnect() noexcept {
		conn.Disconnect();
		update_timer.Cancel();
	}

	/**
	 * Check if the given name exists in the database.
	 */
	bool Lookup(const char *host) const noexcept;

private:
	void OnUpdateTimer() noexcept;

	void ScheduleUpdate() noexcept;

	void UnscheduleUpdate() noexcept {
		update_timer.Cancel();
	}

	void AddAltName(const std::string &common_name,
			std::string &&alt_name) noexcept;
	void RemoveAltName(const std::string &common_name,
			   const std::string &alt_name) noexcept;

	/* virtual methods from Pg::AsyncConnectionHandler */
	void OnConnect() override;
	void OnDisconnect() noexcept override;
	void OnNotify(const char *name) override;
	void OnError(std::exception_ptr e) noexcept override;

	/* virtual methods from Pg::AsyncResultHandler */
	void OnResult(Pg::Result &&result) override;
	void OnResultEnd() override;
	void OnResultError() noexcept override;
};

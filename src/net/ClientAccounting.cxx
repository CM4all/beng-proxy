// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ClientAccounting.hxx"
#include "event/Loop.hxx"
#include "net/SocketAddress.hxx"
#include "util/DeleteDisposer.hxx"

AccountedClientConnection::~AccountedClientConnection() noexcept
{
	if (per_client != nullptr)
		per_client->RemoveConnection(*this);
}

void
AccountedClientConnection::NoteRequest() noexcept
{
	if (per_client != nullptr)
		per_client->NoteRequest();
}

void
AccountedClientConnection::NoteResponseFinished() noexcept
{
	if (per_client != nullptr)
		per_client->NoteResponseFinished();
}

Event::Duration
AccountedClientConnection::GetDelay() const noexcept
{
	return per_client != nullptr
		? per_client->GetDelay()
		: Event::Duration{};
}

inline
PerClientAccounting::PerClientAccounting(ClientAccountingMap &_map,
					 const BareInetAddress &_address) noexcept
	:map(_map), address(_address)
{
}

inline Event::TimePoint
PerClientAccounting::Now() const noexcept
{
	return map.GetEventLoop().SteadyNow();
}

bool
PerClientAccounting::Check() const noexcept
{
	const std::size_t max_connections = map.GetMaxConnections();
	return max_connections == 0 || connections.size() < max_connections;
}

void
PerClientAccounting::AddConnection(AccountedClientConnection &c) noexcept
{
	assert(c.per_client == nullptr);

	connections.push_back(c);
	c.per_client = this;
}

void
PerClientAccounting::RemoveConnection(AccountedClientConnection &c) noexcept
{
	assert(c.per_client == this);

	connections.erase(connections.iterator_to(c));
	c.per_client = nullptr;

	expires = Now() + std::chrono::minutes{5};

	if (connections.empty())
		map.ScheduleCleanup();
}

inline void
PerClientAccounting::NoteRequest() noexcept
{
	static constexpr Event::Duration IDLE_THRESHOLD = std::chrono::seconds{2};
	static constexpr Event::Duration BUSY_THRESHOLD = std::chrono::minutes{2};
	static constexpr Event::Duration TARPIT_FOR = std::chrono::minutes{1};
	static constexpr Event::Duration MAX_DELAY = std::chrono::minutes{1};
	static constexpr Event::Duration DELAY_STEP = std::chrono::milliseconds{500};

	const auto now = Now();

	if (now - idle_since > IDLE_THRESHOLD) {
		busy_since = now;

		if (delay > DELAY_STEP)
			delay -= DELAY_STEP;
	} else if (now - busy_since > BUSY_THRESHOLD) {
		tarpit_until = now + TARPIT_FOR;

		if (delay < MAX_DELAY)
			delay += DELAY_STEP;
	}

	idle_since = now;

	if (now >= tarpit_until)
		delay = {};
}

inline void
PerClientAccounting::NoteResponseFinished() noexcept
{
	idle_since = Now();
}

PerClientAccounting *
ClientAccountingMap::Get(SocketAddress _address) noexcept
{
	BareInetAddress address;
	if (!address.CopyFrom(_address))
		return nullptr;

	auto [i, inserted] = map.insert_check(address);
	if (inserted) {
		auto *per_client = new PerClientAccounting(*this, address);
		map.insert_commit(i, *per_client);
		return per_client;
	} else
		return &*i;

}

void
ClientAccountingMap::ScheduleCleanup() noexcept
{
	if (!cleanup_timer.IsPending())
		cleanup_timer.Schedule(std::chrono::minutes{1});
}

void
ClientAccountingMap::OnCleanupTimer() noexcept
{
	bool reschedule = false;

	const auto now = GetEventLoop().SteadyNow();

	map.remove_and_dispose_if([&reschedule, now](const auto &i){
		if (!i.connections.empty())
			return false;

		if (now < i.expires) {
			reschedule = true;
			return false;
		}

		return true;
	}, DeleteDisposer{});

	if (reschedule)
		ScheduleCleanup();
}

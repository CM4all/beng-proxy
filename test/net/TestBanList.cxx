// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "net/BanList.hxx"
#include "net/ParseBareInetAddress.hxx"
#include "net/BareInetAddress.hxx"
#include "event/Loop.hxx"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using std::string_view_literals::operator""sv;

static BareInetAddress
ParseAddress(std::string_view s) noexcept
{
	BareInetAddress address;
	EXPECT_TRUE(ParseBareInetAddress(address, s)) << s;
	return address;
}

TEST(BanList, Basic)
{
	EventLoop event_loop;
	BanList ban_list{event_loop};

	const auto a = ParseAddress("192.168.1.1"sv);
	const auto b = ParseAddress("10.0.0.1"sv);

	/* nothing is banned initially */
	EXPECT_EQ(ban_list.Get(a), BanAction::NONE);
	EXPECT_EQ(ban_list.Get(b), BanAction::NONE);

	/* ban one address; the other remains unaffected */
	ban_list.Set(a, BanAction::REJECT, std::chrono::minutes{5});
	EXPECT_EQ(ban_list.Get(a), BanAction::REJECT);
	EXPECT_EQ(ban_list.Get(b), BanAction::NONE);

	/* overwrite the ban action */
	ban_list.Set(a, BanAction::TARPIT, std::chrono::minutes{5});
	EXPECT_EQ(ban_list.Get(a), BanAction::TARPIT);

	/* a non-positive duration removes the ban */
	ban_list.Set(a, BanAction::TARPIT, Event::Duration::zero());
	EXPECT_EQ(ban_list.Get(a), BanAction::NONE);

	/* removing a ban that doesn't exist is a no-op */
	ban_list.Set(b, BanAction::REJECT, Event::Duration::zero());
	EXPECT_EQ(ban_list.Get(b), BanAction::NONE);

	ban_list.BeginShutdown();
}

TEST(BanList, Expire)
{
	EventLoop event_loop;
	BanList ban_list{event_loop};

	const auto a = ParseAddress("192.168.1.1"sv);

	ban_list.Set(a, BanAction::REJECT, std::chrono::milliseconds{50});
	EXPECT_EQ(ban_list.Get(a), BanAction::REJECT);

	std::this_thread::sleep_for(std::chrono::milliseconds{200});
	event_loop.FlushClockCaches();

	EXPECT_EQ(ban_list.Get(a), BanAction::NONE);

	ban_list.BeginShutdown();
}

TEST(BanList, Ipv4Network)
{
	EventLoop event_loop;
	BanList ban_list{event_loop};

	const auto network = ParseAddress("192.168.1.0"sv);
	const auto host1 = ParseAddress("192.168.1.5"sv);
	const auto host2 = ParseAddress("192.168.1.200"sv);
	const auto other_subnet = ParseAddress("192.168.2.5"sv);

	/* the /24 network address of host1 and host2 */
	ASSERT_EQ(host1.ToNetwork(128 - 8), network);
	ASSERT_EQ(host2.ToNetwork(128 - 8), network);
	ASSERT_NE(other_subnet.ToNetwork(128 - 8), network);

	EXPECT_EQ(ban_list.Get(host1), BanAction::NONE);
	EXPECT_EQ(ban_list.Get(host2), BanAction::NONE);
	EXPECT_EQ(ban_list.Get(other_subnet), BanAction::NONE);

	/* banning the network affects all hosts in that /24 */
	ban_list.Set(network, BanAction::REJECT, std::chrono::minutes{5});
	EXPECT_EQ(ban_list.Get(host1), BanAction::REJECT);
	EXPECT_EQ(ban_list.Get(host2), BanAction::REJECT);
	EXPECT_EQ(ban_list.Get(other_subnet), BanAction::NONE);

	/* a specific ban on one host takes precedence over the
	   network ban */
	ban_list.Set(host1, BanAction::TARPIT, std::chrono::minutes{5});
	EXPECT_EQ(ban_list.Get(host1), BanAction::TARPIT);
	EXPECT_EQ(ban_list.Get(host2), BanAction::REJECT);
	EXPECT_EQ(ban_list.Get(other_subnet), BanAction::NONE);

	ban_list.BeginShutdown();
}

TEST(BanList, Ipv6Network)
{
	EventLoop event_loop;
	BanList ban_list{event_loop};

	const auto network = ParseAddress("2001:1234:5678:abcd::"sv);
	const auto host1 = ParseAddress("2001:1234:5678:abcd::1"sv);
	const auto host2 = ParseAddress("2001:1234:5678:abcd::2"sv);
	const auto other_subnet = ParseAddress("2001:1234:5678:abce::1"sv);

	/* the /64 network address of host1 and host2 */
	ASSERT_EQ(host1.ToNetwork(64), network);
	ASSERT_EQ(host2.ToNetwork(64), network);
	ASSERT_NE(other_subnet.ToNetwork(64), network);

	ban_list.Set(network, BanAction::TARPIT, std::chrono::minutes{5});
	EXPECT_EQ(ban_list.Get(host1), BanAction::TARPIT);
	EXPECT_EQ(ban_list.Get(host2), BanAction::TARPIT);
	EXPECT_EQ(ban_list.Get(other_subnet), BanAction::NONE);

	ban_list.BeginShutdown();
}

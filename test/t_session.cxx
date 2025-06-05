// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "bp/session/Lease.hxx"
#include "bp/session/Session.hxx"
#include "bp/session/Manager.hxx"
#include "event/Loop.hxx"

#include <gtest/gtest.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

TEST(SessionTest, Basic)
{
	EventLoop event_loop;

	SessionManager session_manager(event_loop, std::chrono::minutes(30),
				       0, 0);

	const auto session_id = session_manager.CreateSession()->id;

	SessionLease session{session_manager, session_id};
	ASSERT_TRUE(session);
	ASSERT_EQ(session->id, session_id);

	auto *realm = session->GetRealm("a_realm_name");
	ASSERT_NE(realm, nullptr);

	auto *widget = realm->GetWidget("a_widget_name", false);
	ASSERT_EQ(widget, nullptr);

	widget = realm->GetWidget("a_widget_name", true);
	ASSERT_NE(widget, nullptr);
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

/**
 * Checks if the specified User-Agent request header is a well-known
 * bot.  This is notoriously unreliable, because we cannot know all
 * bots in the world.  This is just here to optimize session
 * management: don't create sessions for bots.
 */
[[gnu::pure]]
bool
user_agent_is_bot(const char *user_agent) noexcept;

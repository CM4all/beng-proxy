// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class EventLoop;
struct LbConfig;

/**
 * Implementation of --check.
 */
void
lb_check(EventLoop &event_loop, const LbConfig &config);

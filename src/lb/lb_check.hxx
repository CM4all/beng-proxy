// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class EventLoop;
struct LbConfig;

/**
 * Implementation of --check.
 */
void
lb_check(EventLoop &event_loop, const LbConfig &config);

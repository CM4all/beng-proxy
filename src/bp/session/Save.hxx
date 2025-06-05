// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Saving all sessions into a file.
 */

#pragma once

class SessionManager;

void
session_save_init(SessionManager &manager, const char *path) noexcept;

void
session_save_deinit(SessionManager &manager) noexcept;

void
session_save(SessionManager &manager) noexcept;

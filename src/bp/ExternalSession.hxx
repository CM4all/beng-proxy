// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Hooks into external session managers.
 */

#pragma once

struct BpInstance;
struct Session;

/**
 * Check if the external session manager
 * (#TRANSLATE_EXTERNAL_SESSION_KEEPALIVE) needs to be refreshed, and
 * if yes, sends a HTTP GET request (as a background operation).
 */
void
RefreshExternalSession(BpInstance &instance, Session &session);

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Definitions for the session file format.
 */

#pragma once

#include <stdint.h>

static constexpr uint32_t MAGIC_FILE = 2461362039;
static constexpr uint32_t MAGIC_SESSION = 663845835;
static constexpr uint32_t MAGIC_REALM_SESSION = 983957474;
static constexpr uint32_t MAGIC_REALM_SESSION_OLD = 983957473;
static constexpr uint32_t MAGIC_WIDGET_SESSION = 983957472;
static constexpr uint32_t MAGIC_COOKIE = 860919820;
static constexpr uint32_t MAGIC_END_OF_RECORD = 1588449078;
static constexpr uint32_t MAGIC_END_OF_LIST = 1556616445;

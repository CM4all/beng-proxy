// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Utilities for Linux capabilities.
 */

#pragma once

#include <span>

#include <sys/capability.h>

/**
 * Prepare the process for further calls of this library.  Do this
 * right after startup, before initializing anything and before
 * spawning child processes.
 */
void
capabilities_init();

/**
 * Call after setuid().
 */
void
capabilities_post_setuid(std::span<const cap_value_t> keep_list);

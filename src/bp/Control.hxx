// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Handler for control messages.
 */

#pragma once

struct BpInstance;

void
global_control_handler_init(BpInstance *instance);

void
global_control_handler_deinit(BpInstance *instance);

void
local_control_handler_init(BpInstance *instance);

void
local_control_handler_deinit(BpInstance *instance);

void
local_control_handler_open(BpInstance *instance);

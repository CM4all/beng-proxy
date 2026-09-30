// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>

class FileDescriptor;

/**
 * Discard input from the WAS input pipe after PREMATURE was received
 * on the control socket.  Therefore, all announced data must already
 * be in the pipe buffer.
 *
 * Throws on error.
 */
void
DiscardInput(FileDescriptor input, uint_least64_t remaining);

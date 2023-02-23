// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

namespace Co { template<typename T> class Task; }
namespace Uring { class Queue; }

/**
 * Wrapper for statx() which takes a directory path instead of a file
 * descriptor.
 */
Co::Task<struct statx>
CoStatAt(Uring::Queue &queue, const char *directory, const char *pathname, int flags,
	 unsigned mask) noexcept;

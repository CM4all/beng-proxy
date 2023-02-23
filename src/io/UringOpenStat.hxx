// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class AllocatorPtr;
class FileDescriptor;
class CancellablePointer;
namespace Uring { class Queue; class OpenStatHandler; }

/**
 * If #directory is a valid file descriptor, then RESOLVE_BENEATH is
 * used.
 */
void
UringOpenStat(Uring::Queue &uring, AllocatorPtr alloc,
	      FileDescriptor directory,
	      const char *path,
	      Uring::OpenStatHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept;

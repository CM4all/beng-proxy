// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct FileAt;
class AllocatorPtr;
class CancellablePointer;
namespace Uring { class Queue; class OpenHandler; }

/**
 * If #directory is a valid file descriptor, then RESOLVE_BENEATH is
 * used.
 */
void
UringOpen(Uring::Queue &uring, AllocatorPtr alloc,
	  FileAt file, int flags,
	  Uring::OpenHandler &handler,
	  CancellablePointer &cancel_ptr) noexcept;

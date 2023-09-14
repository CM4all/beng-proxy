// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/BindMethod.hxx"

struct FileAt;
class AllocatorPtr;
class UniqueFileDescriptor;
class CancellablePointer;
namespace Uring { class Queue; class OpenStatHandler; }

using UringOpenStatSuccessCallback = BoundMethod<void(UniqueFileDescriptor fd, struct statx &st) noexcept>;
using UringOpenStatErrorCallback = BoundMethod<void(int error) noexcept>;

/**
 * If #directory is a valid file descriptor, then RESOLVE_BENEATH is
 * used.
 */
void
UringOpenStat(Uring::Queue &uring, AllocatorPtr alloc, FileAt file,
	      UringOpenStatSuccessCallback on_success,
	      UringOpenStatErrorCallback on_error,
	      CancellablePointer &cancel_ptr) noexcept;

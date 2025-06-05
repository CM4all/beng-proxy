// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/BindMethod.hxx"

struct FileAt;
class CancellablePointer;
namespace Uring { class Queue; }

using UringStatSuccessCallback = BoundMethod<void(const struct statx &st) noexcept>;
using UringStatErrorCallback = BoundMethod<void(int error) noexcept>;

void
UringStat(Uring::Queue &queue, FileAt file, int flags, unsigned mask,
	  UringStatSuccessCallback on_success,
	  UringStatErrorCallback on_error,
	  CancellablePointer &cancel_ptr) noexcept;

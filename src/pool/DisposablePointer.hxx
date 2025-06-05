// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/DisposablePointer.hxx"
#include "AllocatorPtr.hxx"

template<typename T, typename... Args>
auto
NewDisposablePointer(AllocatorPtr alloc, Args&&... args) noexcept
{
	return ToDestructPointer(alloc.New<T>(std::forward<Args>(args)...));
}

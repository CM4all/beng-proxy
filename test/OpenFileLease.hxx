// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "io/FileDescriptor.hxx"
#include "util/SharedLease.hxx"

#include <cstddef>

struct pool;

std::tuple<FileDescriptor, SharedLease, std::size_t>
OpenFileLease(struct pool &pool, const char *path);

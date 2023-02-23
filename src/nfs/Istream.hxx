// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <stdint.h>

struct pool;
class UnusedIstreamPtr;
class NfsFileHandle;

/*
 * #Istream implementation which reads a file from a NFS server.
 */
UnusedIstreamPtr
istream_nfs_new(struct pool &pool, NfsFileHandle &handle,
		uint64_t start, uint64_t end);

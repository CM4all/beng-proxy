// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef BENG_PROXY_ISTREAM_UNLOCK_HXX
#define BENG_PROXY_ISTREAM_UNLOCK_HXX

struct pool;
class UnusedIstreamPtr;
class CacheItem;

/**
 * An istream facade which unlocks a cache item after it has been
 * closed.
 */
UnusedIstreamPtr
istream_unlock_new(struct pool &pool, UnusedIstreamPtr input, CacheItem &item);

#endif

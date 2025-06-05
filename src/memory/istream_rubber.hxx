// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#ifndef BENG_PROXY_ISTREAM_RUBBER_HXX
#define BENG_PROXY_ISTREAM_RUBBER_HXX

#include <stddef.h>

struct pool;
class UnusedIstreamPtr;
class Rubber;

/**
 * #Istream implementation which reads from a rubber allocation.
 *
 * @param auto_remove shall the allocation be removed when this
 * istream is closed?
 */
UnusedIstreamPtr
istream_rubber_new(struct pool &pool, Rubber &rubber,
		   unsigned id, size_t start, size_t end,
		   bool auto_remove) noexcept;

#endif

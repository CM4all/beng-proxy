// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
class UnusedIstreamPtr;

/*
 * This istream filter passes one byte at a time.  This is useful for
 * testing and debugging istream handler implementations.
 */
UnusedIstreamPtr
istream_byte_new(struct pool &pool, UnusedIstreamPtr input);

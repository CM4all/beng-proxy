// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
struct escape_class;

/**
 * An istream filter that escapes the data.
 */
UnusedIstreamPtr
istream_escape_new(struct pool &pool, UnusedIstreamPtr input,
		   const struct escape_class &cls);

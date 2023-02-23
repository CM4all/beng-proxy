// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;

/**
 * #Istream implementation which reads nothing.
 */
UnusedIstreamPtr
istream_null_new(struct pool &pool);

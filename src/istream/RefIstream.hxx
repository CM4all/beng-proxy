// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class UnusedIstreamPtr;

/**
 * Creates a new #Istream which forwards data as-is from another
 * #Istream and holds a pool reference.
 */
UnusedIstreamPtr
NewRefIstream(struct pool &pool, UnusedIstreamPtr input) noexcept;

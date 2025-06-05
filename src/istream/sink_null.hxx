// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
class UnusedIstreamPtr;

/**
 * An istream handler which silently discards everything and ignores errors.
 */
void
sink_null_new(struct pool &p, UnusedIstreamPtr istream);

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class SinkClose;

/**
 * An istream handler which closes the istream as soon as data
 * arrives.  This is used in the test cases.
 */
SinkClose &
sink_close_new(struct pool &p, UnusedIstreamPtr istream);

void
sink_close_read(SinkClose &sink) noexcept;

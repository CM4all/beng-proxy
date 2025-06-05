// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
class Istream;
class UnusedIstreamPtr;

/**
 * An istream facade which waits for the istream handler to appear.
 * Until then, it blocks all read requests from the inner stream.
 *
 * This class is required because all other istreams require a handler
 * to be installed.  In the case of HTTP proxying, the request body
 * istream has no handler until the connection to the other HTTP
 * server is open.  Meanwhile, istream_hold blocks all read requests
 * from the client's request body.
 */
Istream *
istream_hold_new(struct pool &pool, UnusedIstreamPtr input);

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class NfsCache;
class HttpResponseHandler;
class CancellablePointer;

void
nfs_request(struct pool &pool, NfsCache &nfs_cache,
	    const char *server, const char *export_, const char *path,
	    const char *content_type,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr);

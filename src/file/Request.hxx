// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/uring/config.h" // for HAVE_URING

struct pool;
class HttpResponseHandler;
class EventLoop;
class CancellablePointer;
namespace Uring { class Queue; }

/**
 * Static file support for DirectResourceLoader.
 */
void
static_file_get(EventLoop &event_loop,
#ifdef HAVE_URING
		Uring::Queue *uring,
#endif
		struct pool &pool,
		const char *base,
		const char *path, const char *content_type,
		HttpResponseHandler &handler, CancellablePointer &cancel_ptr);

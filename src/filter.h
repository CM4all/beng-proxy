/*
 * Filter a resource through an HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FILTER_H
#define __BENG_FILTER_H

#include "pool.h"
#include "growing-buffer.h"
#include "istream.h"

struct hstock;
struct http_response_handler;
struct async_operation_ref;

void
filter_new(pool_t pool,
           struct hstock *http_client_stock,
           const char *url,
           growing_buffer_t headers,
           off_t content_length, istream_t body,
           const struct http_response_handler *handler,
           void *handler_ctx,
           struct async_operation_ref *async_ref);

#endif

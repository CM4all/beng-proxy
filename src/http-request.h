/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_REQUEST_H
#define __BENG_HTTP_REQUEST_H

#include "growing-buffer.h"
#include "istream.h"
#include "http.h"

struct hstock;
struct uri_with_address;
struct http_response_handler;
struct async_operation_ref;

void
url_stream_new(pool_t pool,
               struct hstock *http_client_stock,
               http_method_t method,
               struct uri_with_address *uwa,
               growing_buffer_t headers, istream_t body,
               const struct http_response_handler *handler,
               void *handler_ctx,
               struct async_operation_ref *async_ref);

#endif

/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URL_STREAM_H
#define __BENG_URL_STREAM_H

#include "growing-buffer.h"
#include "istream.h"
#include "http.h"

struct hstock;
struct http_response_handler;
struct async_operation_ref;

void
url_stream_new(pool_t pool,
               struct hstock *http_client_stock,
               http_method_t method, const char *url,
               growing_buffer_t headers,
               off_t content_length, istream_t body,
               const struct http_response_handler *handler,
               void *handler_ctx,
               struct async_operation_ref *async_ref);

#endif

/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_REQUEST_H
#define __BENG_HTTP_REQUEST_H

#include "istream.h"
#include "http.h"

struct hstock;
struct uri_with_address;
struct growing_buffer;
struct http_response_handler;
struct async_operation_ref;

void
http_request(pool_t pool,
             struct hstock *tcp_stock,
             http_method_t method,
             struct uri_with_address *uwa,
             struct growing_buffer *headers, istream_t body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref);

#endif

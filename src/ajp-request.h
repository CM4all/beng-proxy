/*
 * High level AJP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_AJP_REQUEST_H
#define __BENG_AJP_REQUEST_H

#include "istream.h"
#include "http.h"

struct hstock;
struct uri_with_address;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

void
ajp_stock_request(pool_t pool,
                  struct hstock *ajp_client_stock,
                  http_method_t method,
                  struct uri_with_address *uwa,
                  struct strmap *headers, istream_t body,
                  const struct http_response_handler *handler,
                  void *handler_ctx,
                  struct async_operation_ref *async_ref);

#endif

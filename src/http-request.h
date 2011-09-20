/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_REQUEST_H
#define __BENG_HTTP_REQUEST_H

#include <http/method.h>

struct pool;
struct istream;
struct tcp_balancer;
struct uri_with_address;
struct growing_buffer;
struct http_response_handler;
struct async_operation_ref;

/**
 * @param session_sticky a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 */
void
http_request(struct pool *pool,
             struct tcp_balancer *tcp_balancer,
             unsigned session_sticky,
             http_method_t method,
             struct uri_with_address *uwa,
             struct growing_buffer *headers, struct istream *body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref);

#endif

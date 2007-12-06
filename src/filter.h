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
#include "http-client.h"

struct hstock;

typedef struct filter *filter_t;

filter_t attr_malloc
filter_new(pool_t pool,
           struct hstock *http_client_stock,
           const char *url,
           growing_buffer_t headers,
           off_t content_length, istream_t body,
           const struct http_response_handler *handler,
           void *handler_ctx);

/**
 * Cancels the transfer.  You must not call this method after the
 * callback has been invoked.
 */
void
filter_close(filter_t us);

#endif

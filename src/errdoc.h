/*
 * Error document handler.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ERRDOC_H
#define BENG_PROXY_ERRDOC_H

#include "http.h"
#include "istream.h"

struct request;
struct growing_buffer;

/**
 * Asks the translation server for an error document, and submits it
 * to response_dispatch().  If there is no error document, or the
 * error document resource fails, it resubmits the original response.
 */
void
errdoc_dispatch_response(struct request *request2, http_status_t status,
                         struct growing_buffer *headers, istream_t body);

#endif

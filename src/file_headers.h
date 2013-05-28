/*
 * Handle the request/response headers for static files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_HEADERS_H
#define BENG_PROXY_FILE_HEADERS_H

#include <stdbool.h>

struct growing_buffer;
struct translate_response;
struct request;
struct stat;
struct file_request;

bool
file_evaluate_request(struct request *request2,
                      int fd, const struct stat *st,
                      struct file_request *file_request);

void
file_cache_headers(struct growing_buffer *headers,
                   int fd, const struct stat *st);

void
file_response_headers(struct growing_buffer *headers,
                      const struct translate_response *tr,
                      int fd, const struct stat *st,
                      bool processor_enabled, bool processor_first);

#endif

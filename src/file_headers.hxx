/*
 * Handle the request/response headers for static files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_HEADERS_HXX
#define BENG_PROXY_FILE_HEADERS_HXX

#include <sys/types.h>

struct growing_buffer;
struct request;
struct stat;

enum range_type {
    RANGE_NONE,
    RANGE_VALID,
    RANGE_INVALID
};

struct file_request {
    enum range_type range;

    off_t skip;
    off_t size;
};

bool
file_evaluate_request(struct request *request2,
                      int fd, const struct stat *st,
                      struct file_request *file_request);

void
file_cache_headers(struct growing_buffer *headers,
                   int fd, const struct stat *st,
                   unsigned expires_relative);

void
file_response_headers(struct growing_buffer *headers,
                      const char *override_content_type,
                      int fd, const struct stat *st,
                      unsigned expires_relative,
                      bool processor_enabled, bool processor_first);

#endif

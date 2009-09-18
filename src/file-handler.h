/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_HANDLER_H
#define BENG_PROXY_FILE_HANDLER_H

#include "istream.h"

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
file_evaluate_request(struct request *request2, const struct stat *st,
                      struct file_request *file_request);

void
file_dispatch(struct request *request2, const struct stat *st,
              const struct file_request *file_request,
              istream_t body);

void
file_callback(struct request *request);

#endif

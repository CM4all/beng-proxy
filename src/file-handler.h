/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_HANDLER_H
#define BENG_PROXY_FILE_HANDLER_H

#include <stdbool.h>
#include <sys/types.h>

struct istream;
struct request;
struct file_request;
struct stat;

void
file_dispatch(struct request *request2, const struct stat *st,
              const struct file_request *file_request,
              struct istream *body);

void
file_callback(struct request *request);

#endif

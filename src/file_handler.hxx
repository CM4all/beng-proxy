/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_HANDLER_HXX
#define BENG_PROXY_FILE_HANDLER_HXX

#include <sys/types.h>

class Istream;
struct Request;
struct file_request;
struct stat;

void
file_dispatch(Request &request2, const struct stat &st,
              const struct file_request &file_request,
              Istream *body);

void
file_callback(Request &request);

#endif

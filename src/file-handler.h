/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_HANDLER_H
#define BENG_PROXY_FILE_HANDLER_H

struct request;

void
file_callback(struct request *request);

#endif

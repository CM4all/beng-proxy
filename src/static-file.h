/*
 * Static file support for resource_get().
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STATIC_FILE_H
#define __BENG_STATIC_FILE_H

#include "pool.h"

struct http_response_handler;
struct async_operation_ref;

void
static_file_get(pool_t pool, const char *path,
                const struct http_response_handler *handler,
                void *handler_ctx);

#endif

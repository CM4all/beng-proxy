/*
 * Static file support for resource_get().
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STATIC_FILE_H
#define __BENG_STATIC_FILE_H

struct pool;
struct http_response_handler;
struct async_operation_ref;

#ifdef __cplusplus
extern "C" {
#endif

void
static_file_get(struct pool *pool, const char *path, const char *content_type,
                const struct http_response_handler *handler,
                void *handler_ctx);

#ifdef __cplusplus
}
#endif

#endif

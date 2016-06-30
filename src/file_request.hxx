/*
 * Static file support for DirectResourceLoader.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILE_REQUEST_HXX
#define BENG_PROXY_FILE_REQUEST_HXX

struct pool;
class HttpResponseHandler;
struct async_operation_ref;
class EventLoop;

void
static_file_get(EventLoop &event_loop, struct pool &pool,
                const char *path, const char *content_type,
                HttpResponseHandler &_handler);

#endif

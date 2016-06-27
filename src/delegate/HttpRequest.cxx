/*
 * This helper library glues delegate_glue and http_response_handler
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "HttpRequest.hxx"
#include "Handler.hxx"
#include "Glue.hxx"
#include "static_headers.hxx"
#include "http_response.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"
#include "istream/istream_file.hxx"
#include "gerrno.h"
#include "pool.hxx"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

class DelegateHttpRequest final : DelegateHandler {
    EventLoop &event_loop;
    struct pool &pool;
    const char *const path;
    const char *const content_type;
    struct http_response_handler_ref handler;

public:
    DelegateHttpRequest(EventLoop &_event_loop, struct pool &_pool,
                        const char *_path, const char *_content_type,
                        const struct http_response_handler &_handler, void *ctx)
        :event_loop(_event_loop), pool(_pool),
         path(_path), content_type(_content_type),
         handler(_handler, ctx) {}

    void Open(StockMap &stock, const char *helper,
              const ChildOptions &options,
              struct async_operation_ref &async_ref) {
        delegate_stock_open(&stock, &pool,
                            helper, options, path,
                            *this, async_ref);
    }

private:
    /* virtual methods from class DelegateHandler */
    void OnDelegateSuccess(int fd) override;

    void OnDelegateError(GError *error) override {
        handler.InvokeAbort(error);
    }
};

void
DelegateHttpRequest::OnDelegateSuccess(int fd)
{
    struct stat st;
    if (fstat(fd, &st) < 0) {
        GError *error = new_error_errno();
        g_prefix_error(&error, "Failed to stat %s: ", path);
        handler.InvokeAbort(error);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);
        handler.InvokeMessage(pool, HTTP_STATUS_NOT_FOUND,
                              "Not a regular file");
        return;
    }

    /* XXX handle if-modified-since, ... */

    auto *headers = strmap_new(&pool);
    static_response_headers(&pool, headers, fd, &st, content_type);

    Istream *body = istream_file_fd_new(event_loop, pool, path,
                                        fd, FdType::FD_FILE,
                                        st.st_size);
    handler.InvokeResponse(HTTP_STATUS_OK, headers, body);
}

void
delegate_stock_request(EventLoop &event_loop, StockMap &stock,
                       struct pool &pool,
                       const char *helper,
                       const ChildOptions &options,
                       const char *path, const char *content_type,
                       const struct http_response_handler *handler, void *ctx,
                       struct async_operation_ref &async_ref)
{
    auto get = NewFromPool<DelegateHttpRequest>(pool, event_loop, pool,
                                                path, content_type,
                                                *handler, ctx);
    get->Open(stock, helper, options, async_ref);
}

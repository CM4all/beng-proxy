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
#include "GException.hxx"
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
    HttpResponseHandler &handler;

public:
    DelegateHttpRequest(EventLoop &_event_loop, struct pool &_pool,
                        const char *_path, const char *_content_type,
                        HttpResponseHandler &_handler)
        :event_loop(_event_loop), pool(_pool),
         path(_path), content_type(_content_type),
         handler(_handler) {}

    void Open(StockMap &stock, const char *helper,
              const ChildOptions &options,
              CancellablePointer &cancel_ptr) {
        delegate_stock_open(&stock, &pool,
                            helper, options, path,
                            *this, cancel_ptr);
    }

private:
    /* virtual methods from class DelegateHandler */
    void OnDelegateSuccess(int fd) override;

    void OnDelegateError(std::exception_ptr ep) override {
        handler.InvokeError(ToGError(ep));
    }
};

void
DelegateHttpRequest::OnDelegateSuccess(int fd)
{
    struct stat st;
    if (fstat(fd, &st) < 0) {
        GError *error = new_error_errno();
        g_prefix_error(&error, "Failed to stat %s: ", path);
        handler.InvokeError(error);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);
        handler.InvokeResponse(pool, HTTP_STATUS_NOT_FOUND,
                               "Not a regular file");
        return;
    }

    /* XXX handle if-modified-since, ... */

    Istream *body = istream_file_fd_new(event_loop, pool, path,
                                        fd, FdType::FD_FILE,
                                        st.st_size);
    handler.InvokeResponse(HTTP_STATUS_OK,
                           static_response_headers(pool, fd, st, content_type),
                           body);
}

void
delegate_stock_request(EventLoop &event_loop, StockMap &stock,
                       struct pool &pool,
                       const char *helper,
                       const ChildOptions &options,
                       const char *path, const char *content_type,
                       HttpResponseHandler &handler,
                       CancellablePointer &cancel_ptr)
{
    auto get = NewFromPool<DelegateHttpRequest>(pool, event_loop, pool,
                                                path, content_type,
                                                handler);
    get->Open(stock, helper, options, cancel_ptr);
}

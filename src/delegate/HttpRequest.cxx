/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "HttpRequest.hxx"
#include "Handler.hxx"
#include "Glue.hxx"
#include "static_headers.hxx"
#include "HttpResponseHandler.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/FileIstream.hxx"
#include "pool/pool.hxx"
#include "system/Error.hxx"
#include "io/UniqueFileDescriptor.hxx"

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
    void OnDelegateSuccess(UniqueFileDescriptor fd) override;

    void OnDelegateError(std::exception_ptr ep) override {
        handler.InvokeError(ep);
    }
};

void
DelegateHttpRequest::OnDelegateSuccess(UniqueFileDescriptor fd)
{
    struct stat st;
    if (fstat(fd.Get(), &st) < 0) {
        handler.InvokeError(std::make_exception_ptr(FormatErrno("Failed to stat %s: ", path)));
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        handler.InvokeResponse(pool, HTTP_STATUS_NOT_FOUND,
                               "Not a regular file");
        return;
    }

    /* XXX handle if-modified-since, ... */

    auto response_headers = static_response_headers(pool, fd.Get(), st,
                                                    content_type);

    Istream *body = istream_file_fd_new(event_loop, pool, path,
                                        fd.Steal(), FdType::FD_FILE,
                                        st.st_size);
    handler.InvokeResponse(HTTP_STATUS_OK,
                           std::move(response_headers),
                           UnusedIstreamPtr(body));
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

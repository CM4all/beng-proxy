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

#include "handler.hxx"
#include "Address.hxx"
#include "Glue.hxx"
#include "file_handler.hxx"
#include "file_headers.hxx"
#include "file_address.hxx"
#include "generate_response.hxx"
#include "bp_instance.hxx"
#include "request.hxx"
#include "http_server/Request.hxx"
#include "http_response.hxx"
#include "istream/istream_file.hxx"

#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * delegate_handler
 *
 */

void
Request::OnDelegateSuccess(int fd)
{
    /* get file information */

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);

        response_dispatch_message(*this, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);

        response_dispatch_message(*this, HTTP_STATUS_NOT_FOUND,
                                  "Not a regular file");
        return;
    }

    /* request options */

    struct file_request file_request(st.st_size);
    if (!file_evaluate_request(*this, fd, st, file_request)) {
        close(fd);
        return;
    }

    /* build the response */

    file_dispatch(*this, st, file_request,
                  istream_file_fd_new(instance.event_loop, pool,
                                      handler.delegate.path,
                                      fd, FdType::FD_FILE,
                                      file_request.range.size));
}

void
Request::OnDelegateError(std::exception_ptr ep)
{
    response_dispatch_log(*this, ep);
}

/*
 * public
 *
 */

void
delegate_handler(Request &request2, const DelegateAddress &address,
                 const char *path)
{
    auto &request = request2.request;

    assert(path != nullptr);

    /* check request */

    if (request.method != HTTP_METHOD_HEAD &&
        request.method != HTTP_METHOD_GET &&
        !request2.processor_focus) {
        method_not_allowed(request2, "GET, HEAD");
        return;
    }

    /* run the delegate helper */

    request2.handler.delegate.path = path;

    delegate_stock_open(request2.instance.delegate_stock, &request.pool,
                        address.delegate, address.child_options,
                        path,
                        request2, request2.cancel_ptr);
}

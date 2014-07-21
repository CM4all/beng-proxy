/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "http_server.hxx"
#include "args.hxx"
#include "strmap.hxx"
#include "istream.h"

bool
request::IsProcessorEnabled() const
{
    return translate.response->views->HasProcessor();
}

void
request_discard_body(struct request *request)
{
    if (request->body != nullptr) {
        struct istream *body = request->body;
        request->body = nullptr;
        istream_close_unused(body);
    }
}

void
request_args_parse(struct request *request)
{
    assert(request != nullptr);
    assert(request->args == nullptr);

    if (strref_is_empty(&request->uri.args)) {
        request->args = nullptr;
        request->translate.request.param = nullptr;
        request->translate.request.session = nullptr;
        return;
    }

    request->args = args_parse(request->request->pool,
                               request->uri.args.data, request->uri.args.length);
    request->translate.request.param = request->args->Remove("translate");
    request->translate.request.session = nullptr;
}

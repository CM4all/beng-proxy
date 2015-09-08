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
#include "istream/istream.hxx"

Request::Request(client_connection &_connection,
                 http_server_request &_request)
    :connection(&_connection),
     request(&_request)
{
    session_id.Clear();
    operation.Init2<Request, &Request::operation>();
}

bool
Request::IsProcessorEnabled() const
{
    return translate.response->views->HasProcessor();
}

void
Request::DiscardRequestBody()
{
    if (body != nullptr) {
        struct istream *old_body = body;
        body = nullptr;
        istream_close_unused(old_body);
    }
}

void
Request::ParseArgs()
{
    assert(args == nullptr);

    if (strref_is_empty(&uri.args)) {
        args = nullptr;
        translate.request.param = nullptr;
        translate.request.session = nullptr;
        return;
    }

    args = args_parse(request->pool, uri.args.data, uri.args.length);
    translate.request.param = args->Remove("translate");
    translate.request.session = nullptr;
}

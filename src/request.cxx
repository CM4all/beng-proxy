/*
 * The BENG request struct.  This is only used by the handlers
 * (handler.c, file-handler.c etc.).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "http_server/Request.hxx"
#include "args.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"

Request::Request(client_connection &_connection,
                 http_server_request &_request)
    :pool(*_request.pool),
     connection(_connection),
     request(_request)
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
        Istream *old_body = body;
        body = nullptr;
        old_body->CloseUnused();
    }
}

void
Request::ParseArgs()
{
    assert(args == nullptr);

    if (uri.args.IsEmpty()) {
        args = nullptr;
        translate.request.param = nullptr;
        translate.request.session = nullptr;
        return;
    }

    args = args_parse(&pool, uri.args.data, uri.args.size);
    translate.request.param = args->Remove("translate");
    translate.request.session = nullptr;
}

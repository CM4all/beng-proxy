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

bool
request::IsProcessorEnabled() const
{
    return translate.response->views->HasProcessor();
}

void
request::DiscardRequestBody()
{
    if (body != nullptr) {
        struct istream *old_body = body;
        body = nullptr;
        istream_close_unused(old_body);
    }
}

void
request::ParseArgs()
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

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

#include "request.hxx"
#include "http_server/Request.hxx"
#include "args.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"

Request::Request(BpInstance &_instance, BpConnection &_connection,
                 HttpServerRequest &_request)
    :pool(_request.pool),
     instance(_instance),
     connection(_connection),
     request(_request)
{
    session_id.Clear();
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

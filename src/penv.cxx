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

#include "penv.hxx"

processor_env::processor_env(struct pool *_pool,
                             EventLoop &_event_loop,
                             ResourceLoader &_resource_loader,
                             ResourceLoader &_filter_resource_loader,
                             const char *_site_name,
                             const char *_untrusted_host,
                             const char *_local_host,
                             const char *_remote_host,
                             const char *_request_uri,
                             const char *_absolute_uri,
                             const DissectedUri *_uri,
                             const StringMap *_args,
                             const char *_session_cookie,
                             SessionId _session_id,
                             const char *_realm,
                             const StringMap *_request_headers)
    :pool(_pool),
     event_loop(&_event_loop),
     resource_loader(&_resource_loader),
     filter_resource_loader(&_filter_resource_loader),
    site_name(_site_name), untrusted_host(_untrusted_host),
    local_host(_local_host), remote_host(_remote_host),
    uri(_request_uri), absolute_uri(_absolute_uri), external_uri(_uri),
     args(_args),
    request_headers(_request_headers),
    session_cookie(_session_cookie),
     session_id(_session_id), realm(_realm) {}

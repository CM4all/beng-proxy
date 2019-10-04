/*
 * Copyright 2007-2019 Content Management AG
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

#pragma once

#include "bp/session/Id.hxx"
#include "util/StringView.hxx"

class EventLoop;
class ResourceLoader;
class StringMap;
class SessionLease;
class RealmSessionLease;

struct WidgetContext {
    EventLoop *event_loop;

    ResourceLoader *resource_loader;
    ResourceLoader *filter_resource_loader;

    const char *site_name;

    /**
     * If non-NULL, then only untrusted widgets with this host are
     * allowed; all trusted widgets are rejected.
     */
    const char *untrusted_host;

    const char *local_host;
    const char *remote_host;

    const char *uri;

    const char *absolute_uri;

    /** the base URI which was requested by the beng-proxy client */
    StringView external_base_uri;

    /** semicolon-arguments in the external URI */
    const StringMap *args;

    const StringMap *request_headers;

    /**
     * The name of the session cookie.
     */
    const char *session_cookie;

    SessionId session_id;
    const char *realm;

    WidgetContext(EventLoop &_event_loop,
                  ResourceLoader &_resource_loader,
                  ResourceLoader &_filter_resource_loader,
                  const char *site_name,
                  const char *untrusted_host,
                  const char *local_host,
                  const char *remote_host,
                  const char *request_uri,
                  const char *absolute_uri,
                  StringView external_base_uri,
                  const StringMap *args,
                  const char *session_cookie,
                  SessionId session_id,
                  const char *realm,
                  const StringMap *request_headers);

    SessionLease GetSession() const;
    RealmSessionLease GetRealmSession() const;
};

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef BENG_PROXY_ISTREAM_HTML_ESCAPE_HXX
#define BENG_PROXY_ISTREAM_HTML_ESCAPE_HXX

struct pool;
class UnusedIstreamPtr;

UnusedIstreamPtr
istream_html_escape_new(struct pool &pool, UnusedIstreamPtr input);

#endif

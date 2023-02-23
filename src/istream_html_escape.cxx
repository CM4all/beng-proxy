// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "istream_html_escape.hxx"
#include "escape/Istream.hxx"
#include "escape/HTML.hxx"
#include "istream/UnusedPtr.hxx"

UnusedIstreamPtr
istream_html_escape_new(struct pool &pool, UnusedIstreamPtr input)
{
    return istream_escape_new(pool, std::move(input), html_escape_class);
}

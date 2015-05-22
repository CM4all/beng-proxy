/*
 * This istream filter which escapes control characters with HTML
 * entities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_html_escape.hxx"
#include "istream_internal.hxx"
#include "istream_escape.hxx"
#include "escape_html.hxx"

struct istream *
istream_html_escape_new(struct pool *pool, struct istream *input)
{
    return istream_escape_new(pool, input, &html_escape_class);
}

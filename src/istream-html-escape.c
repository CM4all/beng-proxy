/*
 * This istream filter which escapes control characters with HTML
 * entities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "istream-escape.h"
#include "escape_html.h"

istream_t
istream_html_escape_new(pool_t pool, istream_t input)
{
    return istream_escape_new(pool, input, &html_escape_class);
}

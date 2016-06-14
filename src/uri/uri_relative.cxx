/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_relative.hxx"
#include "util/StringView.hxx"

#include <string.h>

StringView
uri_relative(StringView base, StringView uri)
{
    if (base.IsEmpty() || uri.IsEmpty())
        return nullptr;

    if (uri.size >= base.size &&
        memcmp(uri.data, base.data, base.size) == 0) {
        uri.skip_front(base.size);
        return uri;
    }

    /* special case: http://hostname without trailing slash */
    if (uri.size == base.size - 1 &&
        memcmp(uri.data, base.data, base.size) &&
        memchr(uri.data + 7, '/', uri.size - 7) == nullptr)
        return StringView::Empty();

    return nullptr;
}

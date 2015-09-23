/*
 * Functions for working with URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_relative.hxx"
#include "strref.h"

#include <string.h>

const struct strref *
uri_relative(const struct strref *base, struct strref *uri)
{
    if (base == nullptr || strref_is_empty(base) ||
        uri == nullptr || strref_is_empty(uri))
        return nullptr;

    if (uri->length >= base->length &&
        memcmp(uri->data, base->data, base->length) == 0) {
        strref_skip(uri, base->length);
        return uri;
    }

    /* special case: http://hostname without trailing slash */
    if (uri->length == base->length - 1 &&
        memcmp(uri->data, base->data, base->length) &&
        memchr(uri->data + 7, '/', uri->length - 7) == nullptr) {
        strref_clear(uri);
        return uri;
    }

    return nullptr;
}

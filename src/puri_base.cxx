/*
 * Functions for working with base URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "puri_base.hxx"
#include "puri_escape.hxx"
#include "AllocatorPtr.hxx"
#include "uri/uri_base.hxx"
#include "util/StringView.hxx"

#include <assert.h>
#include <string.h>

size_t
base_string_unescape(AllocatorPtr alloc, const char *p, const char *tail)
{
    assert(p != nullptr);
    assert(tail != nullptr);

    char *unescaped = uri_unescape_dup(alloc, tail);
    if (unescaped == nullptr)
        /* unescape failure: pretend it's a mismatch */
        return (size_t)-1;

    return base_string(p, unescaped);
}

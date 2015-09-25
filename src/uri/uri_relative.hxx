/*
 * Functions for working with relative URIs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_URI_RELATIVE_HXX
#define BENG_URI_RELATIVE_HXX

struct StringView;

/**
 * Check if an (absolute) URI is relative to an a base URI (also
 * absolute), and return the relative part.  Returns NULL if both URIs
 * do not match.
 */
StringView
uri_relative(StringView base, StringView &uri);

#endif

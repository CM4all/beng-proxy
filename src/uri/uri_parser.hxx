/*
 * Dissect an URI into its parts.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URI_PARSER_HXX
#define BENG_PROXY_URI_PARSER_HXX

#include "util/StringView.hxx"

/**
 * A splitted URI.
 */
struct parsed_uri {
    /**
     * The "base" URI that points to the real resource, without
     * dynamic arguments.
     */
    StringView base;

    /**
     * The beng-proxy arguments, which were introduced by a semicolon
     * (without the semicolon).
     */
    StringView args;

    /**
     * The URI portion after the arguments, including the leading
     * slash.
     */
    StringView path_info;

    /**
     * The query string (without the question mark).
     */
    StringView query;

    /**
     * Split the URI into its parts.  The result contains pointers
     * into the original string.
     */
    bool Parse(const char *src);
};

#endif

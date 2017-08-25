/*
 * Copyright 2007-2017 Content Management AG
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

#include "Class.hxx"
#include "util/RuntimeError.hxx"

#include <string.h>

static void
widget_check_untrusted_host(const char *untrusted_host, const char *host)
{
    assert(untrusted_host != nullptr);

    if (host == nullptr)
        throw FormatRuntimeError("Untrusted widget (required host '%s') not allowed on host",
                                 untrusted_host);

    /* untrusted widget only allowed on matching untrusted host
       name */
    if (strcmp(host, untrusted_host) != 0)
        throw FormatRuntimeError("Untrusted widget (required host '%s') not allowed on '%s'",
                                 untrusted_host, host);
}

static void
widget_check_untrusted_prefix(const char *untrusted_prefix, const char *host)
{
    assert(untrusted_prefix != nullptr);

    if (host == nullptr)
        throw FormatRuntimeError("Untrusted widget (required host prefix '%s.') not allowed on trusted host",
                                 untrusted_prefix);

    size_t length = strlen(untrusted_prefix);
    if (strncmp(host, untrusted_prefix, length) != 0 ||
        host[length] != '.')
        throw FormatRuntimeError("Untrusted widget (required host prefix '%s.') not allowed on '%s'",
                                 untrusted_prefix, host);
}

static void
widget_check_untrusted_site_suffix(const char *untrusted_site_suffix,
                                   const char *host, const char *site_name)
{
    assert(untrusted_site_suffix != nullptr);

    if (site_name == nullptr)
        throw FormatRuntimeError("No site name for untrusted widget (suffix '.%s')",
                                 untrusted_site_suffix);

    if (host == nullptr)
        throw FormatRuntimeError("Untrusted widget (required host '%s.%s') not allowed on trusted host",
                                 site_name, untrusted_site_suffix);

    size_t site_name_length = strlen(site_name);
    if (strncmp(host, site_name, site_name_length) != 0 ||
        host[site_name_length] != '.' ||
        strcmp(host + site_name_length + 1, untrusted_site_suffix) != 0)
        throw FormatRuntimeError("Untrusted widget (required host '%s.%s') not allowed on '%s'",
                                 site_name, untrusted_site_suffix, host);
}

static void
widget_check_untrusted_raw_site_suffix(const char *untrusted_raw_site_suffix,
                                       const char *host, const char *site_name)
{
    assert(untrusted_raw_site_suffix != nullptr);

    if (site_name == nullptr)
        throw FormatRuntimeError("No site name for untrusted widget (suffix '%s')",
                                 untrusted_raw_site_suffix);

    if (host == nullptr)
        throw FormatRuntimeError("Untrusted widget (required host '%s%s') not allowed on trusted host",
                                 site_name, untrusted_raw_site_suffix);

    size_t site_name_length = strlen(site_name);
    if (strncmp(host, site_name, site_name_length) != 0 ||
        strcmp(host + site_name_length, untrusted_raw_site_suffix) != 0)
        throw FormatRuntimeError("Untrusted widget (required host '%s%s') not allowed on '%s'",
                                 site_name, untrusted_raw_site_suffix, host);
}

void
WidgetClass::CheckHost(const char *host, const char *site_name) const
{
    if (untrusted_host != nullptr)
        widget_check_untrusted_host(untrusted_host, host);
    else if (untrusted_prefix != nullptr)
        widget_check_untrusted_prefix(untrusted_prefix, host);
    else if (untrusted_site_suffix != nullptr)
         widget_check_untrusted_site_suffix(untrusted_site_suffix,
                                            host, site_name);
    else if (untrusted_raw_site_suffix != nullptr)
        widget_check_untrusted_raw_site_suffix(untrusted_raw_site_suffix,
                                               host, site_name);
    else if (host != nullptr)
        throw FormatRuntimeError("Trusted widget not allowed on untrusted host '%s'",
                                 host);
}

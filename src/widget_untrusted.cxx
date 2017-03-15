/*
 * Widget declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_class.hxx"

#include <string.h>

static bool
widget_check_untrusted_host(const char *untrusted_host, const char *host)
{
    assert(untrusted_host != nullptr);

    if (host == nullptr)
        /* untrusted widget not allowed on trusted host name */
        return false;

    /* untrusted widget only allowed on matching untrusted host
       name */
    return strcmp(host, untrusted_host) == 0;
}

static bool
widget_check_untrusted_prefix(const char *untrusted_prefix, const char *host)
{
    assert(untrusted_prefix != nullptr);

    if (host == nullptr)
        /* untrusted widget not allowed on trusted host name */
        return false;

    /* untrusted widget only allowed on matching untrusted host
       name */
    size_t length = strlen(untrusted_prefix);
    return memcmp(host, untrusted_prefix, length) == 0 &&
        host[length] == '.';
}

static bool
widget_check_untrusted_site_suffix(const char *untrusted_site_suffix,
                                   const char *host, const char *site_name)
{
    assert(untrusted_site_suffix != nullptr);

    if (host == nullptr || site_name == nullptr)
        /* untrusted widget not allowed on trusted host name */
        return false;

    size_t site_name_length = strlen(site_name);
    return memcmp(host, site_name, site_name_length) == 0 &&
        host[site_name_length] == '.' &&
        strcmp(host + site_name_length + 1,
               untrusted_site_suffix) == 0;
}

static bool
widget_check_untrusted_raw_site_suffix(const char *untrusted_raw_site_suffix,
                                       const char *host, const char *site_name)
{
    assert(untrusted_raw_site_suffix != nullptr);

    if (host == nullptr || site_name == nullptr)
        /* untrusted widget not allowed on trusted host name */
        return false;

    size_t site_name_length = strlen(site_name);
    return memcmp(host, site_name, site_name_length) == 0 &&
        strcmp(host + site_name_length,
               untrusted_raw_site_suffix) == 0;
}

bool
WidgetClass::CheckHost(const char *host, const char *site_name) const
{
    if (untrusted_host != nullptr)
        return widget_check_untrusted_host(untrusted_host, host);
    else if (untrusted_prefix != nullptr)
        return widget_check_untrusted_prefix(untrusted_prefix, host);
    else if (untrusted_site_suffix != nullptr)
        return widget_check_untrusted_site_suffix(untrusted_site_suffix,
                                                  host, site_name);
    else if (untrusted_raw_site_suffix != nullptr)
        return widget_check_untrusted_raw_site_suffix(untrusted_raw_site_suffix,
                                                      host, site_name);
    else
        /* trusted widget is only allowed on a trusted host name
           (host==nullptr) */
        return host == nullptr;
}

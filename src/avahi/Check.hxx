/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef AVAHI_CHECK_HXX
#define AVAHI_CHECK_HXX

#include <string>

struct StringView;

/**
 * Check if the given service name is valid according to RFC 6335.  If
 * not, an exception with an explanatory message is thrown.
 */
void
CheckZeroconfServiceName(StringView name);

/**
 * Check if the given service type is valid according to RFC 6763 and
 * RFC 6335.  If not, an exception with an explanatory message is
 * thrown.
 */
void
CheckZeroconfServiceType(const char *type);

/**
 * Generate a Zeroconf service type from the given configuration
 * string.  If it is a bare service name, an underscore is prepended
 * and the given suffix is appended.
 *
 * Throws exception on error.
 *
 * @param value the configured value

 * @param default_suffix the suffix to be appended to the service name
 * (without the leading dot); valid values are "_tcp" and "_udp"
 */
std::string
MakeZeroconfServiceType(const char *value, const char *default_suffix);

#endif

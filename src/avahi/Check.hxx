/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef AVAHI_CHECK_HXX
#define AVAHI_CHECK_HXX

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

#endif

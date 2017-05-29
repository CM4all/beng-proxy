/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef CERT_WILDCARD_HXX
#define CERT_WILDCARD_HXX

#include <string>

/**
 * Attempt to convert the given "common name" (i.e. host name) to a
 * wild card by replacing the first segment with an asterisk.  Returns
 * an empty string if this is not possible.
 */
std::string
MakeCommonNameWildcard(const char *s);

#endif

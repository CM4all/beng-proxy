/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
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

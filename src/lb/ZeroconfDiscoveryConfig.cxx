/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "ZeroconfDiscoveryConfig.hxx"
#include "lib/avahi/Check.hxx"
#include "lib/avahi/Explorer.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/config/FileLineParser.hxx"
#include "util/StringAPI.hxx"

#include <cassert>

#include <net/if.h>

bool
ZeroconfDiscoveryConfig::ParseLine(const char *word, FileLineParser &line)
{
	if (StringIsEqual(word, "zeroconf_service")) {
		if (!service.empty())
			throw LineParser::Error("Duplicate zeroconf_service");

		service = MakeZeroconfServiceType(line.ExpectValueAndEnd(), "_tcp");
		return true;
	} else if (StringIsEqual(word, "zeroconf_domain")) {
		if (!domain.empty())
			throw LineParser::Error("Duplicate zeroconf_domain");

		domain = line.ExpectValueAndEnd();
		return true;
	} else if (StringIsEqual(word, "zeroconf_interface")) {
		if (service.empty())
			throw LineParser::Error("zeroconf_interface without zeroconf_service");

		if (!interface.empty())
			throw LineParser::Error("Duplicate zeroconf_interface");

		interface = line.ExpectValueAndEnd();
		return true;
	} else if (StringIsEqual(word, "zeroconf_protocol")) {
		if (service.empty())
			throw LineParser::Error("zeroconf_protocol without zeroconf_service");

		if (protocol != AVAHI_PROTO_UNSPEC)
			throw LineParser::Error("Duplicate zeroconf_protocol");

		const char *value = line.ExpectValueAndEnd();
		if (StringIsEqual(value, "inet"))
			protocol = AVAHI_PROTO_INET;
		else if (StringIsEqual(value, "inet6"))
			protocol = AVAHI_PROTO_INET6;
		else
			throw LineParser::Error("Unrecognized zeroconf_protocol");

		return true;
	} else
		return false;
}

void
ZeroconfDiscoveryConfig::Check() const
{
	if (IsEnabled()) {
	} else {
#ifdef HAVE_AVAHI
		if (!domain.empty())
			throw LineParser::Error("zeroconf_service missing");
#endif
	}
}

std::unique_ptr<Avahi::ServiceExplorer>
ZeroconfDiscoveryConfig::Create(Avahi::Client &client,
				Avahi::ServiceExplorerListener &listener,
				Avahi::ErrorHandler &error_handler) const
{
	assert(IsEnabled());

	AvahiIfIndex interface_ = AVAHI_IF_UNSPEC;

	if (!interface.empty()) {
		int i = if_nametoindex(interface.c_str());
		if (i == 0)
			throw FmtErrno("Failed to find interface '{}'", interface);

		interface_ = static_cast<AvahiIfIndex>(i);
	}

	return std::make_unique<Avahi::ServiceExplorer>(client, listener,
							interface_,
							protocol,
							service.c_str(),
							domain.empty() ? nullptr : domain.c_str(),
							error_handler);
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Class.hxx"
#include "lib/fmt/RuntimeError.hxx"

#include <string.h>

static void
widget_check_untrusted_host(const char *untrusted_host, const char *host)
{
	assert(untrusted_host != nullptr);

	if (host == nullptr)
		throw FmtRuntimeError("Untrusted widget (required host '{}') not allowed on host",
				      untrusted_host);

	/* untrusted widget only allowed on matching untrusted host
	   name */
	if (strcmp(host, untrusted_host) != 0)
		throw FmtRuntimeError("Untrusted widget (required host '{}') not allowed on '{}'",
				      untrusted_host, host);
}

static void
widget_check_untrusted_prefix(const char *untrusted_prefix, const char *host)
{
	assert(untrusted_prefix != nullptr);

	if (host == nullptr)
		throw FmtRuntimeError("Untrusted widget (required host prefix '{}.') not allowed on trusted host",
				      untrusted_prefix);

	size_t length = strlen(untrusted_prefix);
	if (strncmp(host, untrusted_prefix, length) != 0 ||
	    host[length] != '.')
		throw FmtRuntimeError("Untrusted widget (required host prefix '{}.') not allowed on '{}'",
				      untrusted_prefix, host);
}

static void
widget_check_untrusted_site_suffix(const char *untrusted_site_suffix,
				   const char *host, const char *site_name)
{
	assert(untrusted_site_suffix != nullptr);

	if (site_name == nullptr)
		throw FmtRuntimeError("No site name for untrusted widget (suffix '.{}')",
				      untrusted_site_suffix);

	if (host == nullptr)
		throw FmtRuntimeError("Untrusted widget (required host '{}.{}') not allowed on trusted host",
				      site_name, untrusted_site_suffix);

	size_t site_name_length = strlen(site_name);
	if (strncasecmp(host, site_name, site_name_length) != 0 ||
	    host[site_name_length] != '.' ||
	    strcmp(host + site_name_length + 1, untrusted_site_suffix) != 0)
		throw FmtRuntimeError("Untrusted widget (required host '{}.{}') not allowed on '{}'",
				      site_name, untrusted_site_suffix, host);
}

static void
widget_check_untrusted_raw_site_suffix(const char *untrusted_raw_site_suffix,
				       const char *host, const char *site_name)
{
	assert(untrusted_raw_site_suffix != nullptr);

	if (site_name == nullptr)
		throw FmtRuntimeError("No site name for untrusted widget (suffix '{}')",
				      untrusted_raw_site_suffix);

	if (host == nullptr)
		throw FmtRuntimeError("Untrusted widget (required host '{}{}') not allowed on trusted host",
				      site_name, untrusted_raw_site_suffix);

	size_t site_name_length = strlen(site_name);
	if (strncasecmp(host, site_name, site_name_length) != 0 ||
	    strcmp(host + site_name_length, untrusted_raw_site_suffix) != 0)
		throw FmtRuntimeError("Untrusted widget (required host '{}{}') not allowed on '{}'",
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
		throw FmtRuntimeError("Trusted widget not allowed on untrusted host '{}'",
				      host);
}

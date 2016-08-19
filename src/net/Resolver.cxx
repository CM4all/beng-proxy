/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Resolver.hxx"
#include "AddressInfo.hxx"

#include <socket/resolver.h>

#include <stdexcept>

AddressInfo
Resolve(const char *host_and_port, int default_port,
	const struct addrinfo *hints)
{
	struct addrinfo *ai;
	int result = socket_resolve_host_port(host_and_port, default_port,
					      hints, &ai);
	if (result != 0) {
		char msg[512];
		snprintf(msg, sizeof(msg),
			 "Failed to resolve '%s': %s",
			 host_and_port, gai_strerror(result));
		throw std::runtime_error(msg);
	}

	return AddressInfo(ai);
}

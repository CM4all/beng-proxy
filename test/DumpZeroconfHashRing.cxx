// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"

#include <algorithm>
#include <array>

#include "util/Compiler.h"
#if CLANG_VERSION >= GCC_MAKE_VERSION(10,0,0)
#pragma GCC diagnostic ignored "-Wkeyword-macro"
#endif

/* horrible kludge which allows us to access private members */
#define class struct
#include "util/HashRing.hxx"
#undef class

#include "lb/MemberHash.hxx"
#include "lib/avahi/Check.hxx"
#include "lib/avahi/Client.hxx"
#include "lib/avahi/ErrorHandler.hxx"
#include "lib/avahi/Explorer.hxx"
#include "lib/avahi/ExplorerListener.hxx"
#include "lib/fmt/SocketAddressFormatter.hxx"
#include "lib/fmt/SystemError.hxx"
#include "event/ShutdownListener.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/PrintException.hxx"

#include <map>
#include <vector>

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h> // for if_nametoindex()

struct Context final : TestInstance, Avahi::ServiceExplorerListener,
		       Avahi::ErrorHandler {
	ShutdownListener shutdown_listener;

	Avahi::Client avahi_client{event_loop, *this};

	Avahi::ServiceExplorer explorer;

	using MemberMap = std::map<std::string, AllocatedSocketAddress>;
	MemberMap members;

	CoarseTimerEvent dump_event;

	Context(AvahiIfIndex zeroconf_interface, const char *zeroconf_service)
		:shutdown_listener(event_loop, BIND_THIS_METHOD(OnShutdown)),
		 explorer(avahi_client, *this,
			  zeroconf_interface, AVAHI_PROTO_UNSPEC,
			  zeroconf_service, nullptr,
			  *this),
		 dump_event(event_loop, BIND_THIS_METHOD(Dump))
	{
		shutdown_listener.Enable();
	}

	void OnShutdown() noexcept {
		shutdown_listener.Disable();
		avahi_client.Close();
	}

	void Dump() noexcept;

	/* virtual methods from class AvahiServiceExplorerListener */
	void OnAvahiNewObject(const std::string &key,
			      SocketAddress address,
			      AvahiStringList *txt) noexcept override;
	void OnAvahiRemoveObject(const std::string &key) noexcept override;

	void OnAvahiAllForNow() noexcept override {
		dump_event.Schedule(std::chrono::seconds{});
	}

	/* virtual methods from class Avahi::ErrorHandler */
	bool OnAvahiError(std::exception_ptr e) noexcept override {
		PrintException(e);
		return false;
	}
};

void
Context::Dump() noexcept
{
	MemberHashRing<MemberMap::value_type> ring;

	BuildMemberHashRing(ring, members,
			    [](MemberMap::const_reference member) noexcept {
				    return member.second;
			    });

	std::map<MemberMap::const_pointer, std::size_t> counts;
	for (const auto &i : ring.buckets)
		++counts[i];

	std::map<std::size_t, MemberMap::const_reference> sorted;
	for (const auto &i : counts)
		sorted.emplace(i.second, *i.first);

	printf("HashRing:\n");

	for (const auto &i : sorted) {
		fmt::print("{:8} {} {}\n", i.first,
			   i.second.first, i.second.second);
	}
}

void
Context::OnAvahiNewObject(const std::string &key,
			  SocketAddress address,
			  [[maybe_unused]] AvahiStringList *txt) noexcept
{
	members.insert_or_assign(key, address);

	dump_event.Schedule(std::chrono::seconds(1));
}

void
Context::OnAvahiRemoveObject(const std::string &key) noexcept
{
	auto i = members.find(key);
	if (i != members.end())
		members.erase(i);

	dump_event.Schedule(std::chrono::seconds(1));
}

static AvahiIfIndex
ParseInterfaceName(const char *name)
{
	int i = if_nametoindex(name);
	if (i == 0)
		throw FmtErrno("Failed to find interface '{}'", name);

	return AvahiIfIndex(i);
}

int
main(int argc, char **argv) noexcept
try {
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s SERVICE [INTERFACE]\n", argv[0]);
		return EXIT_FAILURE;
	}

	const char *const zeroconf_service = argv[1];
	const AvahiIfIndex zeroconf_interface = argc > 2
		? ParseInterfaceName(argv[2])
		: AVAHI_IF_UNSPEC;

	Context ctx(zeroconf_interface,
		    MakeZeroconfServiceType(zeroconf_service, "_tcp").c_str());

	ctx.event_loop.Run();

	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}

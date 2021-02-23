/*
 * Copyright 2007-2021 CM4all GmbH
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
#include "avahi/Check.hxx"
#include "avahi/Client.hxx"
#include "avahi/Explorer.hxx"
#include "avahi/ExplorerListener.hxx"
#include "event/ShutdownListener.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "system/Error.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/ToString.hxx"
#include "util/PrintException.hxx"
#include "PInstance.hxx"

#include <map>
#include <vector>

#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h> // for if_nametoindex()

struct Context final : PInstance, Avahi::ServiceExplorerListener {
	ShutdownListener shutdown_listener;

	Avahi::Client avahi_client{event_loop, "DumpZeroconfHashRing"};

	Avahi::ServiceExplorer explorer;

	using MemberMap = std::map<std::string, AllocatedSocketAddress>;
	MemberMap members;

	CoarseTimerEvent dump_event;

	Context(AvahiIfIndex zeroconf_interface, const char *zeroconf_service)
		:shutdown_listener(event_loop, BIND_THIS_METHOD(OnShutdown)),
		 explorer(avahi_client, *this,
			  zeroconf_interface, AVAHI_PROTO_UNSPEC,
			  zeroconf_service, nullptr),
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
			      SocketAddress address) noexcept override;
	void OnAvahiRemoveObject(const std::string &key) noexcept override;
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
		char buffer[1024];

		printf("%8zu %s %s\n", i.first,
		       i.second.first.c_str(),
		       ToString(buffer, sizeof(buffer), i.second.second,
				"unknown"));
	}
}

void
Context::OnAvahiNewObject(const std::string &key,
			  SocketAddress address) noexcept
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
		throw FormatErrno("Failed to find interface '%s'", name);

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

	ctx.event_loop.Dispatch();

	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}

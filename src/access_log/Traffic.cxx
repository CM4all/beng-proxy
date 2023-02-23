// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Print the site name and the bytes transferred for each request.
 */

#include "Server.hxx"
#include "net/log/Datagram.hxx"

#include <stdio.h>

static void
dump(const Net::Log::Datagram &d)
{
	if (d.site != nullptr && d.valid_traffic)
		printf("%s %llu\n", d.site,
		       (unsigned long long)(d.traffic_received + d.traffic_sent));
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	AccessLogServer().Run(dump);
	return 0;
}

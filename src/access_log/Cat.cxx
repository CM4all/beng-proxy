// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * An example server for the logging protocol.  It prints the messages
 * to stdout.
 */

#include "Server.hxx"
#include "net/log/OneLine.hxx"
#include "io/FileDescriptor.hxx"

#include <unistd.h>

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	const FileDescriptor fd(STDOUT_FILENO);

	constexpr Net::Log::OneLineOptions options{
		.show_site = true,
	};

	AccessLogServer().Run([fd, options](const auto &d){
		Net::Log::LogOneLine(fd, d, options);
	});

	return 0;
}

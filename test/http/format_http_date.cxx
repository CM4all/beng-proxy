// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "http/Date.hxx"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int
main(int argc, char **argv) noexcept
{
	auto t = argc >= 2
		? std::chrono::system_clock::from_time_t(strtoul(argv[1], nullptr, 10))
		: std::chrono::system_clock::now();

	printf("%s\n", http_date_format(t));
}

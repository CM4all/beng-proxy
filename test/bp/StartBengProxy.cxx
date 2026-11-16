// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "StartBengProxy.hxx"
#include "lib/curl/Easy.hxx"
#include "system/Error.hxx"
#include "system/linux/clone3.h"
#include "io/UniqueFileDescriptor.hxx"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <stdlib.h> // for exit()
#include <unistd.h>

using std::string_view_literals::operator""sv;

CurlEasy
BengProxyProcess::CreateCurl(const char *url)
{
	CurlEasy easy{url};
	easy.SetOption(CURLOPT_UNIX_SOCKET_PATH, http_listener.GetPath());

	/* CURL shall not sanitize the request URI; this unit
	   test passes broken URIs intentionally */
	easy.SetOption(CURLOPT_PATH_AS_IS, 1L);

	return easy;
}

BengProxyProcess
StartBengProxy(const char *config_file, const char *listener_tag)
{
	TempListener http_listener;
	auto http_listener_socket = http_listener.Create(SOCK_STREAM, 16);

	TempListener translation_listener;
	auto translation_listener_socket = translation_listener.Create(SOCK_STREAM, 16);

	uintptr_t pidfd;
	struct clone_args ca{
		.flags = CLONE_PIDFD,
		.pidfd = (uintptr_t)&pidfd,
		.exit_signal = SIGCHLD,
	};

	int pid = clone3(&ca, sizeof(ca));
	if (pid < 0)
		throw MakeErrno(-pid, "clone3() failed");

	if (pid == 0) {
		http_listener_socket.CheckDuplicate(FileDescriptor{STDIN_FILENO});

		execl("./cm4all-beng-proxy", "cm4all-beng-proxy",
		      "--config-file", config_file,
		      "--debug-listener-tag", listener_tag,
		      "--translation-socket", translation_listener.GetPath(),
		      nullptr);
		fmt::print(stderr, "exec failed: {}\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	return {
		.pidfd = UniqueFileDescriptor{AdoptTag{}, static_cast<int>(pidfd)},
		.http_listener = std::move(http_listener),
		.translation_listener = std::move(translation_listener),
		.translation_listener_socket = std::move(translation_listener_socket),
	};
}

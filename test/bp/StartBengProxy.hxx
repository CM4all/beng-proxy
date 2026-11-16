// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "AutoKillPidfd.hxx"
#include "net/TempListener.hxx"
#include "net/UniqueSocketDescriptor.hxx"

class CurlEasy;

struct BengProxyProcess {
	AutoKillPidfd pidfd;

	TempListener http_listener;

	TempListener translation_listener;

	UniqueSocketDescriptor translation_listener_socket;

	CurlEasy CreateCurl(const char *url);
};

BengProxyProcess
StartBengProxy(const char *config_file, const char *listener_tag);

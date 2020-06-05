/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "Launch.hxx"
#include "Address.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "strmap.hxx"
#include "product.h"
#include "spawn/IstreamSpawn.hxx"
#include "spawn/Prepared.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/CharUtil.hxx"
#include "AllocatorPtr.hxx"

#include <sys/wait.h>
#include <string.h>

static const char *
cgi_address_name(const CgiAddress *address)
{
	if (address->interpreter != nullptr)
		return address->interpreter;

	if (address->action != nullptr)
		return address->action;

	if (address->path != nullptr)
		return address->path;

	return "CGI";
}

static constexpr const char *
StringFallback(const char *value, const char *fallback)
{
	return value != nullptr ? value : fallback;
}

/**
 * Throws std::runtime_error on error.
 */
static void
PrepareCgi(struct pool &pool, PreparedChildProcess &p,
	   http_method_t method,
	   const CgiAddress &address,
	   const char *remote_addr,
	   const StringMap &headers,
	   off_t content_length)
{
	const char *path = address.path;

	p.SetEnv("GATEWAY_INTERFACE", "CGI/1.1");
	p.SetEnv("SERVER_PROTOCOL", "HTTP/1.1");
	p.SetEnv("REQUEST_METHOD", http_method_to_string(method));
	p.SetEnv("SCRIPT_FILENAME", path);
	p.SetEnv("PATH_TRANSLATED", path);
	p.SetEnv("REQUEST_URI", address.GetURI(pool));
	p.SetEnv("SCRIPT_NAME", StringFallback(address.script_name, ""));
	p.SetEnv("PATH_INFO", StringFallback(address.path_info, ""));
	p.SetEnv("QUERY_STRING", StringFallback(address.query_string, ""));
	p.SetEnv("DOCUMENT_ROOT",
		 StringFallback(address.document_root, "/var/www"));
	p.SetEnv("SERVER_SOFTWARE", PRODUCT_TOKEN);

	if (remote_addr != nullptr)
		p.SetEnv("REMOTE_ADDR", remote_addr);

	const char *arg = nullptr;
	if (address.action != nullptr)
		path = address.action;

	if (address.interpreter != nullptr) {
		arg = path;
		path = address.interpreter;
	}

	const char *content_type = nullptr;
	for (const auto &pair : headers) {
		if (strcmp(pair.key, "content-type") == 0) {
			content_type = pair.value;
			continue;
		}

		if (strcmp(pair.key, "proxy") == 0)
			/* work around vulnerability in several CGI programs
			   which take the environment variable HTTP_PROXY as
			   proxy specification for their internal HTTP
			   clients; see CVE-2016-5385 and others */
			continue;

		if (strcmp(pair.key, "x-cm4all-https") == 0)
			/* this will be translated to HTTPS */
			continue;

		char buffer[512] = "HTTP_";
		size_t i;
		for (i = 0; 5 + i < sizeof(buffer) - 1 && pair.key[i] != 0; ++i) {
			if (IsLowerAlphaASCII(pair.key[i]))
				buffer[5 + i] = (char)(pair.key[i] - 'a' + 'A');
			else if (IsUpperAlphaASCII(pair.key[i]) ||
				 IsDigitASCII(pair.key[i]))
				buffer[5 + i] = pair.key[i];
			else
				buffer[5 + i] = '_';
		}

		buffer[5 + i] = 0;
		p.SetEnv(buffer, pair.value);
	}

	if (content_type != nullptr)
		p.SetEnv("CONTENT_TYPE", content_type);

	if (content_length >= 0) {
		char value[32];
		snprintf(value, sizeof(value), "%llu",
			 (unsigned long long)content_length);
		p.SetEnv("CONTENT_LENGTH", value);
	}

	const char *https = headers.Get("x-cm4all-https");
	if (https != nullptr && strcmp(https, "on") == 0)
		p.SetEnv("HTTPS", "on");

	p.Append(path);
	for (auto i : address.args)
		p.Append(i);
	if (arg != nullptr)
		p.Append(arg);

	address.options.CopyTo(p);
}

UnusedIstreamPtr
cgi_launch(EventLoop &event_loop, struct pool *pool,
	   http_method_t method,
	   const CgiAddress *address,
	   const char *remote_addr,
	   const StringMap &headers, UnusedIstreamPtr body,
	   SpawnService &spawn_service)
{
	PreparedChildProcess p;
	PrepareCgi(*pool, p, method,
		   *address, remote_addr, headers,
		   body ? body.GetAvailable(false) : -1);

	return SpawnChildProcess(event_loop, pool,
				 cgi_address_name(address), std::move(body),
				 std::move(p),
				 spawn_service);
}

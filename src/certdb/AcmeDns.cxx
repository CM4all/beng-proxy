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

#include "AcmeDns.hxx"
#include "AcmeConfig.hxx"
#include "AcmeHttp.hxx"
#include "ssl/Base64.hxx"
#include "system/Error.hxx"
#include "util/PrintException.hxx"
#include "util/RuntimeError.hxx"

#include <sys/wait.h>
#include <unistd.h>

struct AcmeConfig;

void
SetDnsTxt(const AcmeConfig &config, const char *host, const char *value)
{
	pid_t pid = fork();
	if (pid < 0)
		throw MakeErrno("fork() failed");

	char *const args[] = {
		const_cast<char *>(config.dns_txt_program.c_str()),
		const_cast<char *>(host),
		const_cast<char *>(value),
		nullptr
	};

	if (pid == 0) {
		execve(args[0], args, nullptr);
		perror("execve() failed");
		_exit(EXIT_FAILURE);
	}

	int status;
	pid_t w = waitpid(pid, &status, 0);
	if (w < 0)
		throw MakeErrno("waitpid() failed");

	if (WIFSIGNALED(status))
		throw FormatRuntimeError("%s was killed by signal %d",
					 args[0], WTERMSIG(status));

	if (!WIFEXITED(status))
		throw FormatRuntimeError("%s did not exit", args[0]);

	status = WEXITSTATUS(status);
	if (status != 0)
		throw FormatRuntimeError("%s exited with status %d",
					 args[0], status);
}

void
SetDns01(const AcmeConfig &config, const char *host,
	 const AcmeChallenge &challenge, EVP_PKEY &account_key)
{
	SetDnsTxt(config, host,
		  UrlSafeBase64SHA256(MakeHttp01(challenge, account_key)).c_str());
}

Dns01ChallengeRecord::Dns01ChallengeRecord(const AcmeConfig &_config,
					   const std::string &_host,
					   const AcmeChallenge &challenge,
					   EVP_PKEY &account_key)
	:config(_config), host(_host)
{
	SetDns01(config, host.c_str(), challenge, account_key);
}

Dns01ChallengeRecord::~Dns01ChallengeRecord() noexcept
{
	try {
		SetDnsTxt(config, host.c_str(), nullptr);
	} catch (...) {
		fprintf(stderr, "Failed to remove TXT record of '%s': ",
			host.c_str());
		PrintException(std::current_exception());
	}
}

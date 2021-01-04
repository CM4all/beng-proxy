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

#include "AcmeDns.hxx"
#include "AcmeConfig.hxx"
#include "AcmeHttp.hxx"
#include "sodium/UrlSafeBase64SHA256.hxx"
#include "system/Error.hxx"
#include "util/PrintException.hxx"
#include "util/RuntimeError.hxx"

#include <sys/wait.h>
#include <unistd.h>

struct AcmeConfig;

static void
SetDnsTxt(const AcmeConfig &config, const char *host,
	  const std::set<std::string> &values)
{
	pid_t pid = fork();
	if (pid < 0)
		throw MakeErrno("fork() failed");

	char *args[32];
	size_t n = 0;

	args[n++] = const_cast<char *>(config.dns_txt_program.c_str());
	args[n++] = const_cast<char *>(host);

	for (const auto &i : values) {
		args[n++] = const_cast<char *>(i.c_str());
		if (n >= std::size(args))
			throw std::runtime_error("Too many TXT records");
	}

	args[n] = nullptr;

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

Dns01ChallengeRecord::~Dns01ChallengeRecord() noexcept
{
	if (!must_clear)
		return;

	try {
		SetDnsTxt(config, host.c_str(), {});
	} catch (...) {
		fprintf(stderr, "Failed to remove TXT record of '%s': ",
			host.c_str());
		PrintException(std::current_exception());
	}
}

void
Dns01ChallengeRecord::AddChallenge(const AcmeChallenge &challenge,
				   EVP_PKEY &account_key)
{
	values.emplace(UrlSafeBase64SHA256(MakeHttp01(challenge, account_key)));
}

void
Dns01ChallengeRecord::Commit()
{
	must_clear = true;
	SetDnsTxt(config, host.c_str(), values);
}

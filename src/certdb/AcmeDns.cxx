// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AcmeDns.hxx"
#include "AcmeConfig.hxx"
#include "AcmeHttp.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/sodium/UrlSafeBase64SHA256.hxx"
#include "system/Error.hxx"
#include "util/PrintException.hxx"
#include "util/SpanCast.hxx"

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
		throw FmtRuntimeError("{} was killed by signal {}",
				      args[0], WTERMSIG(status));

	if (!WIFEXITED(status))
		throw FmtRuntimeError("{} did not exit", args[0]);

	status = WEXITSTATUS(status);
	if (status != 0)
		throw FmtRuntimeError("{} exited with status {}",
				      args[0], status);
}

Dns01ChallengeRecord::~Dns01ChallengeRecord() noexcept
{
	if (!must_clear)
		return;

	try {
		SetDnsTxt(config, host.c_str(), {});
	} catch (...) {
		fmt::print(stderr, "Failed to remove TXT record of '{}': ", host);
		PrintException(std::current_exception());
	}
}

void
Dns01ChallengeRecord::AddChallenge(const AcmeChallenge &challenge,
				   const EVP_PKEY &account_key)
{
	values.emplace(UrlSafeBase64SHA256(AsBytes(MakeHttp01(challenge, account_key))));
}

void
Dns01ChallengeRecord::Commit()
{
	must_clear = true;
	SetDnsTxt(config, host.c_str(), values);
}

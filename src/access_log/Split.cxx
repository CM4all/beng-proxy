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

/*
 * This logging server splits the log file into many, e.g. you may
 * have one log file per site.
 */

#include "Server.hxx"
#include "net/log/OneLine.hxx"
#include "time/Convert.hxx"
#include "util/ConstBuffer.hxx"

#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

static bool use_local_time = false;

static bool
string_equals(const char *a, size_t a_length, const char *b)
{
	size_t b_length = strlen(b);
	return a_length == b_length && memcmp(a, b, a_length) == 0;
}

static auto
SplitTimePoint(std::chrono::system_clock::time_point tp)
{
	return use_local_time
		? LocalTime(tp)
		: GmTime(tp);
}

static const char *
expand_timestamp(const char *fmt, const Net::Log::Datagram &d)
{
	if (!d.HasTimestamp())
		return nullptr;

	try {
		const auto tm = SplitTimePoint(Net::Log::ToSystem(d.timestamp));
		static char buffer[64];
		strftime(buffer, sizeof(buffer), fmt, &tm);
		return buffer;
	} catch (...) {
		/* just in case GmTime() throws */
		return nullptr;
	}
}

static const char *
expand(const char *name, size_t length, const Net::Log::Datagram &d)
{
	if (string_equals(name, length, "site"))
		return d.site;
	else if (string_equals(name, length, "date"))
		return expand_timestamp("%Y-%m-%d", d);
	else if (string_equals(name, length, "year"))
		return expand_timestamp("%Y", d);
	else if (string_equals(name, length, "month"))
		return expand_timestamp("%m", d);
	else if (string_equals(name, length, "day"))
		return expand_timestamp("%d", d);
	else if (string_equals(name, length, "hour"))
		return expand_timestamp("%H", d);
	else if (string_equals(name, length, "minute"))
		return expand_timestamp("%M", d);
	else
		return nullptr;
}

static const char *
generate_path(const char *template_, const Net::Log::Datagram &d)
{
	static char buffer[8192];
	char *dest = buffer;

	while (true) {
		const char *escape = strchr(template_, '%');
		if (escape == nullptr) {
			strcpy(dest, template_);
			return buffer;
		}

		if (dest + (escape - template_) + 1 >= buffer + sizeof(buffer))
			/* too long */
			return nullptr;

		memcpy(dest, template_, escape - template_);
		dest += escape - template_;
		template_ = escape + 1;
		if (*template_ != '{') {
			*dest++ = *escape;
			continue;
		}

		++template_;
		const char *end = strchr(template_, '}');
		if (end == nullptr)
			return nullptr;

		const char *value = expand(template_, end - template_, d);
		if (value == nullptr)
			return nullptr;

		size_t length = strlen(value);
		if (dest + length >= buffer + sizeof(buffer))
			/* too long */
			return nullptr;

		memcpy(dest, value, length);
		dest += length;

		template_ = end + 1;
	}
}

static bool
make_parent_directory_recursive(char *path)
{
	char *slash = strrchr(path, '/');
	if (slash == nullptr || slash == path)
		return true;

	*slash = 0;
	int ret = mkdir(path, 0777);
	if (ret >= 0) {
		/* success */
		*slash = '/';
		return true;
	} else if (errno == ENOENT) {
		if (!make_parent_directory_recursive(path))
			return false;

		/* try again */
		ret = mkdir(path, 0777);
		*slash = '/';
		return ret >= 0;
	} else {
		fprintf(stderr, "Failed to create directory %s: %s\n",
			path, strerror(errno));

		return false;
	}
}

static bool
make_parent_directory(const char *path)
{
	char buffer[PATH_MAX];
	if (strlen(path) >= sizeof(buffer)) {
		fprintf(stderr, "Path too long\n");
		return false;
	}

	strcpy(buffer, path);

	return make_parent_directory_recursive(buffer);
}

static FileDescriptor
open_log_file(const char *path)
{
	static FileDescriptor cache_fd = FileDescriptor::Undefined();
	static char cache_path[PATH_MAX];

	if (cache_fd.IsDefined()) {
		if (strcmp(path, cache_path) == 0)
			return cache_fd;

		cache_fd.Close();
	}

	if (!cache_fd.Open(path, O_CREAT|O_APPEND|O_WRONLY, 0666) &&
	    errno == ENOENT) {
		if (!make_parent_directory(path))
			return cache_fd;

		/* try again */
		cache_fd.Open(path, O_CREAT|O_APPEND|O_WRONLY, 0666);
	}

	if (!cache_fd.IsDefined()) {
		fprintf(stderr, "Failed to open %s: %s\n",
			path, strerror(errno));
		return cache_fd;
	}

	strcpy(cache_path, path);

	return cache_fd;
}

static bool
Dump(const char *template_path, const Net::Log::Datagram &d)
{
	const char *path = generate_path(template_path, d);
	if (path == nullptr)
		return false;

	auto fd = open_log_file(path);
	if (fd.IsDefined())
		LogOneLine(fd, d);

	return true;
}

int main(int argc, char **argv)
{
	int argi = 1;
	if (argi < argc && strcmp(argv[argi], "--localtime") == 0) {
		++argi;
		use_local_time = true;
	}

	if (argi >= argc) {
		fprintf(stderr, "Usage: log-split [--localtime] TEMPLATE [...]\n");
		return EXIT_FAILURE;
	}

	ConstBuffer<const char *> templates(&argv[argi], argc - argi);

	AccessLogServer().Run([templates](const Net::Log::Datagram &d){
		for (const char *t : templates)
			if (Dump(t, d))
				break;
	});

	return 0;
}

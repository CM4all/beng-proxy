// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "RuntimeDirectory.hxx"
#include "system/Error.hxx"

#include <cassert>
#include <string_view>

#include <stdio.h>
#include <stdlib.h> // for mktemp()
#include <string.h>
#include <sys/stat.h> // for mkdir()

using std::string_view_literals::operator""sv;

const char *
MakePrivateRuntimeDirectoryTemp(std::span<char> buffer,
				const char *filename_template,
				const char *tmp_directory_template)
{
	assert(buffer.data() != nullptr);
	assert(buffer.size() >= 2);
	assert(filename_template != nullptr);
	assert(strchr(filename_template, '/') == nullptr);
	assert(std::string_view{filename_template}.ends_with("XXXXXX"sv));
	assert(tmp_directory_template != nullptr);
	assert(strchr(tmp_directory_template, '/') == nullptr);
	assert(std::string_view{tmp_directory_template}.ends_with("XXXXXX"sv));

	const char *runtime_directory = getenv("RUNTIME_DIRECTORY");
	if (runtime_directory != nullptr) {
		sprintf(buffer.data(), "%s/private", runtime_directory);
		if (mkdir(buffer.data(), 0700) < 0) {
			const int e = errno;
			if (e != EEXIST)
				throw MakeErrno(e, "Failed to create private directory");
		}
	} else {
		sprintf(buffer.data(), "/tmp/%s", tmp_directory_template);
		if (mkdtemp(buffer.data()) == nullptr)
			throw MakeErrno("mkdtemp() failed");
	}

	const std::size_t length = strlen(buffer.data());
	sprintf(buffer.data() + length, "/%s", filename_template);

	if (*mktemp(buffer.data()) == 0)
		throw MakeErrno("mktemp() failed");

	return buffer.data();
}

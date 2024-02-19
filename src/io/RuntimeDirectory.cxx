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

using std::string_view_literals::operator""sv;

const char *
MakeRuntimeDirectoryTemp(std::span<char> buffer,
			 const char *runtime_directory_template,
			 const char *tmp_directory_template)
{
	assert(buffer.data() != nullptr);
	assert(buffer.size() >= 2);
	assert(runtime_directory_template != nullptr);
	assert(strchr(runtime_directory_template, '/') == nullptr);
	assert(std::string_view{runtime_directory_template}.ends_with("XXXXXX"sv));
	assert(tmp_directory_template != nullptr);
	assert(strchr(tmp_directory_template, '/') == nullptr);
	assert(std::string_view{tmp_directory_template}.ends_with("XXXXXX"sv));

	const char *runtime_directory = getenv("RUNTIME_DIRECTORY");
	if (runtime_directory != nullptr) {
		sprintf(buffer.data(), "%s/%s",
			runtime_directory, runtime_directory_template);
	} else {
		sprintf(buffer.data(), "/tmp/%s", tmp_directory_template);
	}

	if (*mktemp(buffer.data()) == 0)
		throw MakeErrno("mktemp() failed");

	return buffer.data();
}

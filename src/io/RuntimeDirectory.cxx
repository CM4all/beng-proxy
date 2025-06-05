// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "RuntimeDirectory.hxx"
#include "system/Error.hxx"

#include <fmt/core.h>

#include <cassert>
#include <string_view>

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

	char *const path = buffer.data();

	const char *runtime_directory = getenv("RUNTIME_DIRECTORY");
	if (runtime_directory != nullptr) {
		const auto r = fmt::format_to_n(buffer.begin(), buffer.size(),
						"{}/private"sv, runtime_directory);
		if (r.size >= buffer.size())
			throw std::invalid_argument{"Buffer too small"};

		*r.out = 0;

		if (mkdir(path, 0700) < 0) {
			const int e = errno;
			if (e != EEXIST)
				throw MakeErrno(e, "Failed to create private directory");
		}

		buffer = buffer.subspan(r.size);
	} else {
		const auto r = fmt::format_to_n(buffer.begin(), buffer.size(),
						"/tmp/{}"sv, tmp_directory_template);
		if (r.size >= buffer.size())
			throw std::invalid_argument{"Buffer too small"};

		*r.out = 0;

		if (mkdtemp(path) == nullptr)
			throw MakeErrno("mkdtemp() failed");

		buffer = buffer.subspan(r.size);
	}

	const auto r = fmt::format_to_n(buffer.begin(), buffer.size(),
					"/{}"sv, filename_template);
	if (r.size >= buffer.size())
		throw std::invalid_argument{"Buffer too small"};

	*r.out = 0;

	if (*mktemp(path) == 0)
		throw MakeErrno("mktemp() failed");

	return path;
}

// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <span>

/**
 * Generate an absolute path of a temporary file in the application's
 * runtime directory (or fall back to /tmp if $RUNTIME_DIRECTORY is
 * not set).
 *
 * Throws on error.
 *
 * @param filename_template a filename template for
 * $RUNTIME_DIRECTORY, ending with "XXXXXX"
 * @param tmp_directory_template a directory name template for
 * /tmp, ending with "XXXXXX"
 * @return the absolute path name (equals to buffer.data())
 */
const char *
MakePrivateRuntimeDirectoryTemp(std::span<char> buffer,
				const char *filename_template,
				const char *tmp_directory_template);

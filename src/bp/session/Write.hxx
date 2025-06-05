// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Write a session to a file.
 */

#pragma once

#include <stdint.h>

struct Session;
class BufferedOutputStream;

/**
 * Throws on error.
 */
void
session_write_magic(BufferedOutputStream &os, uint32_t magic);

/**
 * Throws on error.
 */
void
session_write_file_header(BufferedOutputStream &os);

/**
 * Throws on error.
 */
void
session_write_file_tail(BufferedOutputStream &os);

/**
 * Throws on error.
 */
void
session_write(BufferedOutputStream &os, const Session *session);

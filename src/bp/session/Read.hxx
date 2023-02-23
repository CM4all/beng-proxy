// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Read sessions from a file.
 */

#pragma once

#include "Prng.hxx"

#include <memory>

#include <stdint.h>

struct Session;
class BufferedReader;

class SessionDeserializerError {};

/**
 * Throws on error.
 */
uint32_t
session_read_magic(BufferedReader &r);

/**
 * Throws on error.
 */
void
session_read_file_header(BufferedReader &r);

/**
 * Throws on error.
 */
std::unique_ptr<Session>
session_read(BufferedReader &r, SessionPrng &prng);

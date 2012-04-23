/*
 * Read sessions from a file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_READ_H
#define BENG_PROXY_SESSION_READ_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct dpool;
struct session;

uint32_t
session_read_magic(FILE *file);

bool
session_read_file_header(FILE *file);

struct session *
session_read(FILE *file, struct dpool *pool);

#endif

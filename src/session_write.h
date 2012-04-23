/*
 * Write a session to a file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_WRITE_H
#define BENG_PROXY_SESSION_WRITE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

struct session;

bool
session_write_magic(FILE *file, uint32_t magic);

bool
session_write_file_header(FILE *file);

bool
session_write_file_tail(FILE *file);

bool
session_write(FILE *file, const struct session *session);

#endif

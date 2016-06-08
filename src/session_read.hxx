/*
 * Read sessions from a file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_READ_HXX
#define BENG_PROXY_SESSION_READ_HXX

#include <stdint.h>
#include <stdio.h>

struct dpool;
struct Session;

uint32_t
session_read_magic(FILE *file);

bool
session_read_file_header(FILE *file);

/**
 * @param old read #MAGIC_SESSION_OLD format (i.e. pre v10.21)
 */
Session *
session_read(FILE *file, struct dpool *pool, bool old);

#endif

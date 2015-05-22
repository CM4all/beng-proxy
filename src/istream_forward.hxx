/*
 * Functions for istream filters which just forward the input.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_FORWARD_H
#define __BENG_ISTREAM_FORWARD_H

#include "istream-direct.h"
#include "glibfwd.hxx"

#include <stddef.h>
#include <sys/types.h>

size_t
istream_forward_data(const void *data, size_t length, void *ctx);

ssize_t
istream_forward_direct(enum istream_direct type, int fd, size_t max_length,
                       void *ctx);

void
istream_forward_eof(void *ctx);

void
istream_forward_abort(GError *error, void *ctx);

extern const struct istream_handler istream_forward_handler;

#endif

/*
 * Utilities for file descriptors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FD_UTIL_H
#define __BENG_FD_UTIL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int
fd_mask_status_flags(int fd, int and_mask, int xor_mask);

int
fd_set_nonblock(int fd, bool value);

bool
fd_ready_for_writing(int fd);

#ifdef __cplusplus
}
#endif

#endif

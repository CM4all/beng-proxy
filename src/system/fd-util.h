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

bool
fd_ready_for_writing(int fd);

#ifdef __cplusplus
}
#endif

#endif

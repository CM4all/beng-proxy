/*
 * Utilities for file descriptors.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FD_UTIL_H
#define __BENG_FD_UTIL_H

int
fd_mask_descriptor_flags(int fd, int and_mask, int xor_mask);

int
fd_mask_status_flags(int fd, int and_mask, int xor_mask);

#endif

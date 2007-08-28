/*
 * Prototype for the splice() system call (Linux only).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SPLICE_H
#define __BENG_SPLICE_H

#ifndef __linux__
#error splice() is Linux specific
#endif

#include <sys/syscall.h>
#include <unistd.h>

#ifndef SPLICE_F_MOVE
#define SPLICE_F_MOVE           0x01
#endif
#ifndef SPLICE_F_NONBLOCK
#define SPLICE_F_NONBLOCK       0x02
#endif
#ifndef SPLICE_F_MORE
#define SPLICE_F_MORE           0x04
#endif
#ifndef SPLICE_F_GIFT
#define SPLICE_F_GIFT           0x08
#endif
#ifndef __NR_splice
#define __NR_splice             313
#endif

static inline ssize_t
splice(int fdin, loff_t *off_in, int fdout, loff_t *off_out,  
       size_t len, unsigned int flags)
{
    return (ssize_t)syscall(__NR_splice, fdin, off_in, fdout, off_out, len, flags);
}

#endif

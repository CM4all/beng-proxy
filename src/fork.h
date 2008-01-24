/*
 * Fork a process and connect its stdin and stdout to istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FORK_H
#define __BENG_FORK_H

#include "istream.h"

pid_t
beng_fork(pool_t pool, istream_t input, istream_t *output_r);

#endif

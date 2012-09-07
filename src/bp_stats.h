/*
 * Collect statistics of a beng-lb process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STATS_H
#define BENG_PROXY_STATS_H

#include <stdint.h>

struct instance;
struct beng_control_stats;

void
bp_get_stats(const struct instance *instance,
             struct beng_control_stats *data);

#endif

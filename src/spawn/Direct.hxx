/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SPAWN_DIRECT_HXX
#define SPAWN_DIRECT_HXX

#include <sys/types.h>

struct PreparedChildProcess;
struct SpawnConfig;

/**
 * @return the process id, or a negative errno value
 */
pid_t
SpawnChildProcess(PreparedChildProcess &&params, const SpawnConfig &config);

#endif

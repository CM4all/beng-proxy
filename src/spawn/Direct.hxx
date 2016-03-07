/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SPAWN_DIRECT_HXX
#define SPAWN_DIRECT_HXX

#include <sys/types.h>

struct PreparedChildProcess;
struct SpawnConfig;
struct CgroupState;

/**
 * @return the process id, or a negative errno value
 */
pid_t
SpawnChildProcess(PreparedChildProcess &&params, const SpawnConfig &config,
                  const CgroupState &cgroup_state);

#endif

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_SERVER_HXX
#define BENG_PROXY_SPAWN_SERVER_HXX

struct SpawnConfig;
struct CgroupState;

void
RunSpawnServer(const SpawnConfig &config, const CgroupState &cgroup_state,
               int fd);

#endif

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_LAUNCH_HXX
#define BENG_PROXY_SPAWN_LAUNCH_HXX

#include <functional>

#include <sys/types.h>

struct SpawnConfig;

pid_t
LaunchSpawnServer(const SpawnConfig &config, int fd,
                  std::function<void()> post_clone);

#endif

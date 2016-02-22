/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_SERVER_HXX
#define BENG_PROXY_SPAWN_SERVER_HXX

struct SpawnConfig;

void
RunSpawnServer(const SpawnConfig &config, int fd);

#endif

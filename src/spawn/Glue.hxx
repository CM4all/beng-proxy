/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_GLUE_HXX
#define BENG_PROXY_SPAWN_GLUE_HXX

#include <functional>

class SpawnServerClient;

SpawnServerClient *
StartSpawnServer(std::function<void()> post_clone);

#endif

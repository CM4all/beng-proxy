/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_GLUE_HXX
#define BENG_PROXY_SPAWN_GLUE_HXX

#include <functional>

struct SpawnConfig;
class SpawnServerClient;
class ChildProcessRegistry;

SpawnServerClient *
StartSpawnServer(const SpawnConfig &config,
                 ChildProcessRegistry &child_process_registry,
                 std::function<void()> post_clone);

#endif

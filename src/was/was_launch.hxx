/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LAUNCH_HXX
#define BENG_PROXY_WAS_LAUNCH_HXX

#include "glibfwd.hxx"

class SpawnService;
class ExitListener;
struct ChildOptions;
template<typename T> struct ConstBuffer;

struct WasProcess {
    int pid = -1;
    int control_fd = -1, input_fd = -1, output_fd = -1;

    void Close();
};

bool
was_launch(SpawnService &spawn_service,
           WasProcess *process,
           const char *name,
           const char *executable_path,
           ConstBuffer<const char *> args,
           const ChildOptions &options,
           ExitListener *listener,
           GError **error_r);

#endif

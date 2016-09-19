/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LAUNCH_HXX
#define BENG_PROXY_WAS_LAUNCH_HXX

#include "io/UniqueFileDescriptor.hxx"
#include "glibfwd.hxx"

class SpawnService;
class ExitListener;
struct ChildOptions;
template<typename T> struct ConstBuffer;

struct WasProcess {
    int pid = -1;
    UniqueFileDescriptor control, input, output;

    bool IsDefined() const {
        return pid > 0;
    }

    void Close() {
        control.Close();
        input.Close();
        output.Close();
    }
};

WasProcess
was_launch(SpawnService &spawn_service,
           const char *name,
           const char *executable_path,
           ConstBuffer<const char *> args,
           const ChildOptions &options,
           ExitListener *listener,
           GError **error_r);

#endif

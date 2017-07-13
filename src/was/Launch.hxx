/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LAUNCH_HXX
#define BENG_PROXY_WAS_LAUNCH_HXX

#include "io/UniqueFileDescriptor.hxx"
#include "net/UniqueSocketDescriptor.hxx"

class SpawnService;
class ExitListener;
struct ChildOptions;
template<typename T> struct ConstBuffer;

struct WasProcess {
    int pid;
    UniqueSocketDescriptor control;
    UniqueFileDescriptor input, output;

    void Close() {
        control.Close();
        input.Close();
        output.Close();
    }
};

/**
 * Throws std::runtime_error on error.
 */
WasProcess
was_launch(SpawnService &spawn_service,
           const char *name,
           const char *executable_path,
           ConstBuffer<const char *> args,
           const ChildOptions &options,
           ExitListener *listener);

#endif

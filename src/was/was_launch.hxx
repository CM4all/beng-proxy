/*
 * Launch WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_LAUNCH_HXX
#define BENG_PROXY_WAS_LAUNCH_HXX

#include "glibfwd.hxx"

#include <sys/types.h>

struct ChildOptions;
template<typename T> struct ConstBuffer;

struct WasProcess {
    pid_t pid = -1;
    int control_fd = -1, input_fd = -1, output_fd = -1;

    void Close();
};

bool
was_launch(WasProcess *process,
           const char *executable_path,
           ConstBuffer<const char *> args,
           ConstBuffer<const char *> env,
           const ChildOptions &options,
           GError **error_r);

#endif

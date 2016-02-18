/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_manager.hxx"
#include "pool.hxx"
#include "spawn/Registry.hxx"
#include "event/SignalEvent.hxx"

#include <daemon/daemonize.h>

#include <assert.h>
#include <sys/wait.h>
#include <sys/resource.h>

static const struct timeval child_kill_timeout = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static bool shutdown_flag = false;
static ChildProcessRegistry *registry;
static SignalEvent sigchld_event;

static void
children_check_shutdown()
{
    if (shutdown_flag && registry->IsEmpty())
        sigchld_event.Delete();
}

static void
child_event_callback(int fd gcc_unused, short event gcc_unused,
                     void *ctx gcc_unused)
{
    pid_t pid;
    int status;

    struct rusage rusage;
    while ((pid = wait4(-1, &status, WNOHANG, &rusage)) > 0) {
        if (daemonize_child_exited(pid, status))
            continue;

        registry->OnExit(pid, status, rusage);

        children_check_shutdown();
    }

    pool_commit();
}

void
children_init()
{
    assert(!shutdown_flag);
    assert(registry == nullptr);
    registry = new ChildProcessRegistry();

    sigchld_event.Set(SIGCHLD, child_event_callback, nullptr);
    sigchld_event.Add();
}

void
children_deinit()
{
    sigchld_event.Delete();
    shutdown_flag = false;

    delete registry;
    registry = nullptr;
}

void
children_clear()
{
    registry->Clear();
}

void
children_shutdown(void)
{
    shutdown_flag = true;

    if (registry->IsEmpty())
        sigchld_event.Delete();
}

void
child_register(pid_t pid, const char *name,
               ExitListener *listener)
{
    assert(!shutdown_flag);
    assert(name != nullptr);

    registry->Add(pid, name, listener);
}

void
child_kill_signal(pid_t pid, int signo)
{
    registry->Kill(pid, signo);
    children_check_shutdown();
}

void
child_kill(pid_t pid)
{
    registry->Kill(pid);
    children_check_shutdown();
}

unsigned
child_get_count(void)
{
    return registry->GetCount();
}

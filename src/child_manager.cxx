/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_manager.hxx"
#include "spawn/Registry.hxx"

#include <daemon/daemonize.h>

#include <assert.h>

static ChildProcessRegistry *registry;

void
children_init()
{
    assert(registry == nullptr);
    registry = new ChildProcessRegistry();
}

void
children_deinit()
{
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
    registry->Shutdown();
}

void
child_register(pid_t pid, const char *name,
               ExitListener *listener)
{
    assert(name != nullptr);

    registry->Add(pid, name, listener);
}

void
child_kill_signal(pid_t pid, int signo)
{
    registry->Kill(pid, signo);
}

void
child_kill(pid_t pid)
{
    registry->Kill(pid);
}

unsigned
child_get_count(void)
{
    return registry->GetCount();
}

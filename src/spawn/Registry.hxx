/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_REGISTRY_HXX
#define BENG_PROXY_SPAWN_REGISTRY_HXX

#include "event/TimerEvent.hxx"
#include "event/SignalEvent.hxx"

#include <inline/compiler.h>

#include <boost/intrusive/set.hpp>

#include <string>
#include <chrono>

#include <assert.h>
#include <sys/types.h>
#include <stdint.h>

class ExitListener;

/**
 * Multiplexer for SIGCHLD.
 */
class ChildProcessRegistry {

    struct ChildProcess
        : boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

        const pid_t pid;

        const std::string name;

        /**
         * The time when this child process was started (registered in
         * this library).
         */
        const std::chrono::steady_clock::time_point start_time;

        ExitListener *listener;

        /**
         * This timer is set up by child_kill_signal().  If the child
         * process hasn't exited after a certain amount of time, we send
         * SIGKILL.
         */
        TimerEvent kill_timeout_event;

        ChildProcess(EventLoop &event_loop,
                     pid_t _pid, const char *_name,
                     ExitListener *_listener);

        void Disable() {
            kill_timeout_event.Cancel();
        }

        void OnExit(int status, const struct rusage &rusage);

        void KillTimeoutCallback();

        struct Compare {
            bool operator()(const ChildProcess &a, const ChildProcess &b) const {
                return a.pid < b.pid;
            }

            bool operator()(const ChildProcess &a, pid_t b) const {
                return a.pid < b;
            }

            bool operator()(pid_t a, const ChildProcess &b) const {
                return a < b.pid;
            }
        };
    };

    EventLoop &event_loop;

    typedef boost::intrusive::set<ChildProcess,
                                  boost::intrusive::compare<ChildProcess::Compare>,
                                  boost::intrusive::constant_time_size<true>> ChildProcessSet;

    ChildProcessSet children;

    SignalEvent sigchld_event;

    /**
     * Shall the #sigchld_event be disabled automatically when there
     * is no registered child process?  This mode should be enabled
     * during shutdown.
     */
    bool volatile_event = false;

public:
    ChildProcessRegistry(EventLoop &loop);

    EventLoop &GetEventLoop() {
        return event_loop;
    }

    bool IsEmpty() const {
        return children.empty();
    }

    /**
     * Forget all registered children.  Call this in the new child process
     * after forking.
     */
    void Clear();

    /**
     * @param name a symbolic name for the process to be used in log
     * messages
     */
    void Add(pid_t pid, const char *name, ExitListener *listener);

    void SetExitListener(pid_t pid, ExitListener *listener);

    /**
     * Send a signal to a child process and unregister it.
     */
    void Kill(pid_t pid, int signo);

    /**
     * Send a SIGTERM to a child process and unregister it.
     */
    void Kill(pid_t pid);

    /**
     * Begin shutdown of this subsystem: wait for all children to exit,
     * and then remove the event.
     */
    void SetVolatile() {
        volatile_event = true;
        CheckVolatileEvent();
    }

    /**
     * Returns the number of registered child processes.
     */
    gcc_pure
    unsigned GetCount() const {
        return children.size();
    }

private:
    gcc_pure
    ChildProcessSet::iterator FindByPid(pid_t pid) {
        return children.find(pid, ChildProcess::Compare());
    }

    void Remove(ChildProcessSet::iterator i) {
        assert(!children.empty());

        i->Disable();

        children.erase(i);
    }

    void CheckVolatileEvent() {
        if (volatile_event && IsEmpty())
            sigchld_event.Disable();
    }

    void OnExit(pid_t pid, int status, const struct rusage &rusage);
    void OnSigChld(int signo);
};

#endif

/*
 * Utilities for managing the mask of blocked signals.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SIGSET_H
#define BENG_PROXY_SIGSET_H

#include <signal.h>

/**
 * Blocks all critical signals.  Call this function to avoid race
 * conditions in the child process after forking.  After the work is
 * done, call leave_signal_section() with the same #sigset_t pointer.
 */
static inline void
enter_signal_section(sigset_t *buffer)
{
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGTERM);
    sigaddset(&s, SIGINT);
    sigaddset(&s, SIGQUIT);
    sigaddset(&s, SIGCHLD);

    sigprocmask(SIG_BLOCK, &s, buffer);
}

/**
 * This function undos the effect of enter_signal_section().  The
 * parent process should call it right after fork() returned the child
 * pid.  The child process should adjust its signal handlers and then
 * call this function.
 */
static inline void
leave_signal_section(const sigset_t *buffer)
{
    sigprocmask(SIG_SETMASK, buffer, NULL);
}

/**
 * Install the system default signal handlers.  To be called before
 * leave_signal_section().  This is useful to prepare executing
 * another program in the forked child process, to close the race
 * condition gap.
 */
static inline void
install_default_signal_handlers(void)
{
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

#endif

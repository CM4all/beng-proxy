/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SYSTEMD_HXX
#define BENG_PROXY_SYSTEMD_HXX

struct CgroupState;

/**
 * Create a new systemd scope and move the current process into it.
 *
 * Throws std::runtime_error on error.
 */
CgroupState
CreateSystemdScope(const char *name, const char *description,
                   int pid, bool delegate=false);

#endif

/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SYSTEMD_HXX
#define BENG_PROXY_SYSTEMD_HXX

/**
 * Create a new systemd scope and move the current process into it.
 *
 * Throws std::runtime_error on error.
 */
void
CreateSystemdScope(const char *name, const char *description,
                   bool delegate=false);

#endif

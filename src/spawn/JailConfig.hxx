/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_JAIL_CONFIG_HXX
#define BENG_PROXY_JAIL_CONFIG_HXX

#include <inline/compiler.h>

#include <string>

class AllocatorPtr;

struct JailConfig {
    std::string root_dir;
    std::string jailed_home;

    /**
     * Loads the JailCGI configuration file, usually located in
     * /etc/cm4all/jailcgi/jail.conf.
     *
     * @return true on success, false on error
     */
    bool Load(const char *path);

    /**
     * Translates a path to a path inside the jail.
     *
     * @return the path inside the jail, allocated from the pool, or
     * nullptr if the specified path cannot be translated
     */
    gcc_pure
    const char *TranslatePath(const char *path, const char *document_root,
                              AllocatorPtr alloc) const;
};

#endif

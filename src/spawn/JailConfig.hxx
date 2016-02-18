/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_JAIL_CONFIG_HXX
#define BENG_PROXY_JAIL_CONFIG_HXX

#include <inline/compiler.h>

struct pool;

struct JailConfig {
    const char *root_dir;
    const char *jailed_home;
};

/**
 * Loads the JailCGI configuration file, usually located in
 * /etc/cm4all/jailcgi/jail.conf.
 *
 * @return true on success, false on error
 */
bool
jail_config_load(JailConfig *config, const char *path,
                 struct pool *pool);

/**
 * Translates a path to a path inside the jail.
 *
 * @return the path inside the jail, allocated from the pool, or NULL
 * if the specified path cannot be translated
 */
gcc_pure
const char *
jail_translate_path(const JailConfig *config, const char *path,
                    const char *document_root, struct pool *pool);

#endif

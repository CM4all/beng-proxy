/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_JAIL_H
#define BENG_PROXY_JAIL_H

#include <inline/compiler.h>

#include <glib.h>

#include <stdbool.h>

struct pool;
struct exec;

struct jail_config {
    const char *root_dir;
    const char *jailed_home;
};

struct jail_params {
    bool enabled;
    const char *account_id;
    const char *site_id;
    const char *user_name;
    const char *host_name;
    const char *home_directory;
};

/**
 * Loads the JailCGI configuration file, usually located in
 * /etc/cm4all/jailcgi/jail.conf.
 *
 * @return true on success, false on error
 */
bool
jail_config_load(struct jail_config *config, const char *path,
                 struct pool *pool);

bool
jail_params_check(const struct jail_params *jail, GError **error_r);

void
jail_params_copy(struct pool *pool, struct jail_params *dest,
                 const struct jail_params *src);

char *
jail_params_id(const struct jail_params *params, char *p);

/**
 * Translates a path to a path inside the jail.
 *
 * @return the path inside the jail, allocated from the pool, or NULL
 * if the specified path cannot be translated
 */
gcc_pure
const char *
jail_translate_path(const struct jail_config *config, const char *path,
                    const char *document_root, struct pool *pool);

void
jail_wrapper_insert(struct exec *e, const struct jail_params *params,
                    const char *document_root);

#endif

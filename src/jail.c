/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "jail.h"
#include "strutil.h"
#include "exec.h"
#include "pool.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

gcc_const
static GQuark
jail_quark(void)
{
    return g_quark_from_static_string("jail");
}

static char *
next_word(char *p)
{
    while (*p != 0 && !char_is_whitespace(*p))
        ++p;

    if (*p == 0)
        return NULL;

    *p++ = 0;

    while (*p != 0 && char_is_whitespace(*p))
        ++p;

    if (*p == 0)
        return NULL;

    return p;
}

bool
jail_config_load(struct jail_config *config, const char *path,
                 struct pool *pool)
{
    FILE *file = fopen(path, "r");
    if (file == NULL)
        return false;

    config->root_dir = NULL;
    config->jailed_home = NULL;

    char line[4096], *p, *q;
    while ((p = fgets(line, sizeof(line), file)) != NULL) {
        while (*p != 0 && char_is_whitespace(*p))
            ++p;

        if (*p == 0 || *p == '#')
            /* ignore comments */
            continue;

        q = next_word(p);
        if (q == NULL || next_word(q) != NULL)
            /* silently ignore syntax errors */
            continue;

        if (strcmp(p, "RootDir") == 0)
            config->root_dir = p_strdup(pool, q);
        else if (strcmp(p, "JailedHome") == 0)
            config->jailed_home = p_strdup(pool, q);
    }

    fclose(file);
    return true;
}

static const char *
jail_try_translate_path(const char *path,
                        const char *global_prefix, const char *jailed_prefix,
                        struct pool *pool)
{
    if (jailed_prefix == NULL)
        return NULL;

    size_t global_prefix_length = strlen(global_prefix);
    if (memcmp(path, global_prefix, global_prefix_length) != 0)
        return NULL;

    if (path[global_prefix_length] == '/')
        return p_strcat(pool, jailed_prefix, path + global_prefix_length,
                        NULL);
    else if (path[global_prefix_length] == 0)
        return jailed_prefix;
    else
        return NULL;
}

bool
jail_params_check(const struct jail_params *jail, GError **error_r)
{
    if (!jail->enabled)
        return true;

    if (jail->home_directory == NULL) {
        g_set_error(error_r, jail_quark(), 0, "No JailCGI home directory");
        return false;
    }

    return true;
}

void
jail_params_copy(struct pool *pool, struct jail_params *dest,
                 const struct jail_params *src)
{
    dest->enabled = src->enabled;

    dest->account_id = p_strdup_checked(pool, src->account_id);
    dest->site_id = p_strdup_checked(pool, src->site_id);
    dest->user_name = p_strdup_checked(pool, src->user_name);
    dest->host_name = p_strdup_checked(pool, src->host_name);
    dest->home_directory = p_strdup_checked(pool, src->home_directory);
}

const char *
jail_translate_path(const struct jail_config *config, const char *path,
                    const char *document_root, struct pool *pool)
{
    const char *translated =
        jail_try_translate_path(path, document_root, config->jailed_home,
                                pool);
    if (translated == NULL)
        translated = jail_try_translate_path(path, config->root_dir, "", pool);
    return translated;
}

void
jail_wrapper_insert(struct exec *e, const struct jail_params *params,
                    const char *document_root)
{
    if (params == NULL || !params->enabled)
        return;

    exec_append(e, "/usr/lib/cm4all/jailcgi/bin/wrapper");

    if (document_root != NULL) {
        exec_append(e, "-d");
        exec_append(e, document_root);
    }

    if (params->account_id != NULL) {
        exec_append(e, "--account");
        exec_append(e, params->account_id);
    }

    if (params->site_id != NULL) {
        exec_append(e, "--site");
        exec_append(e, params->site_id);
    }

    if (params->user_name != NULL) {
        exec_append(e, "--name");
        exec_append(e, params->user_name);
    }

    if (params->host_name != NULL)
        setenv("JAILCGI_SERVERNAME", params->host_name, true);

    if (params->home_directory != NULL) {
        exec_append(e, "--home");
        exec_append(e, params->home_directory);
    }
}

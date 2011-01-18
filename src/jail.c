/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "jail.h"
#include "strutil.h"
#include "exec.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
jail_config_load(struct jail_config *config, const char *path, pool_t pool)
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
                        pool_t pool)
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

const char *
jail_translate_path(const struct jail_config *config, const char *path,
                    const char *document_root, pool_t pool)
{
    const char *translated =
        jail_try_translate_path(path, document_root, config->jailed_home,
                                pool);
    if (translated == NULL)
        translated = jail_try_translate_path(path, config->root_dir, "", pool);
    return translated;
}

void
jail_wrapper_insert(struct exec *e, const char *document_root,
                    const char *account_id, const char *site_id,
                    const char *user_name, const char *host_name,
                    const char *home_directory)
{
    assert(document_root != NULL);

    exec_append(e, "/usr/lib/cm4all/jailcgi/bin/wrapper");
    exec_append(e, "-d");
    exec_append(e, document_root);

    if (account_id != NULL) {
        exec_append(e, "--account");
        exec_append(e, account_id);
    }

    if (site_id != NULL) {
        exec_append(e, "--site");
        exec_append(e, site_id);
    }

    if (user_name != NULL) {
        exec_append(e, "--name");
        exec_append(e, user_name);
    }

    if (host_name != NULL)
        setenv("JAILCGI_SERVERNAME", host_name, true);

    if (home_directory != NULL) {
        exec_append(e, "--home");
        exec_append(e, home_directory);
    }
}

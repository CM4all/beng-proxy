/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "jail.h"
#include "strutil.h"

#include <stdio.h>
#include <string.h>

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
    size_t global_prefix_length = strlen(global_prefix);

    return jailed_prefix != NULL &&
        strncmp(path, global_prefix, global_prefix_length) == 0 &&
        path[global_prefix_length] == '/'
        ? p_strcat(pool, jailed_prefix, path + global_prefix_length, NULL)
        : NULL;
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

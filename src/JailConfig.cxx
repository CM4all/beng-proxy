/*
 * JailCGI integration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "JailConfig.hxx"
#include "pool.hxx"
#include "util/CharUtil.hxx"

#include <stdio.h>
#include <string.h>

static char *
next_word(char *p)
{
    while (!IsWhitespaceOrNull(*p))
        ++p;

    if (*p == 0)
        return nullptr;

    *p++ = 0;

    while (IsWhitespaceNotNull(*p))
        ++p;

    if (*p == 0)
        return nullptr;

    return p;
}

bool
jail_config_load(struct jail_config *config, const char *path,
                 struct pool *pool)
{
    FILE *file = fopen(path, "r");
    if (file == nullptr)
        return false;

    config->root_dir = nullptr;
    config->jailed_home = nullptr;

    char line[4096], *p, *q;
    while ((p = fgets(line, sizeof(line), file)) != nullptr) {
        while (IsWhitespaceNotNull(*p))
            ++p;

        if (*p == 0 || *p == '#')
            /* ignore comments */
            continue;

        q = next_word(p);
        if (q == nullptr || next_word(q) != nullptr)
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
    if (jailed_prefix == nullptr)
        return nullptr;

    size_t global_prefix_length = strlen(global_prefix);
    if (memcmp(path, global_prefix, global_prefix_length) != 0)
        return nullptr;

    if (path[global_prefix_length] == '/')
        return p_strcat(pool, jailed_prefix, path + global_prefix_length,
                        nullptr);
    else if (path[global_prefix_length] == 0)
        return jailed_prefix;
    else
        return nullptr;
}

const char *
jail_translate_path(const struct jail_config *config, const char *path,
                    const char *document_root, struct pool *pool)
{
    const char *translated =
        jail_try_translate_path(path, document_root, config->jailed_home,
                                pool);
    if (translated == nullptr)
        translated = jail_try_translate_path(path, config->root_dir, "", pool);
    return translated;
}
